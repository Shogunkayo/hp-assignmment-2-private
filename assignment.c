#include <assert.h>
#include <stdio.h>
#include <omp.h>
#include <stdlib.h>

#define NUM_PROCS 4
#define CACHE_SIZE 4
#define MEM_SIZE 16
#define MSG_BUFFER_SIZE 256
#define MAX_INSTR_NUM 32

typedef unsigned char byte;

typedef enum { MODIFIED, EXCLUSIVE, SHARED, INVALID } cacheLineState;

// In addition to cache line states, each directory entry also has a state
// EM ( exclusive or modified ) : memory block is in only one cache.
//                  When a memory block in a cache is written to, it would have
//                  resulted in cache hit and transition from EXCLUSIVE to MODIFIED.
//                  Main memory / directory has no way of knowing of this transition.
//                  Hence, this state only cares that the memory block is in a single
//                  cache, and not whether its in EXCLUSIVE or MODIFIED.
// S ( shared )     : memory block is in multiple caches
// U ( unowned )    : memory block is not in any cache
typedef enum { EM, S, U } directoryEntryState;

typedef enum { 
    READ_REQUEST,       // requesting node sends to home node on a read miss            done
    WRITE_REQUEST,      // requesting node sends to home node on a write miss           done
    REPLY_RD,           // home node replies with data to requestor for read request    done
    REPLY_WR,           // home node replies to requestor for write request             done    
    REPLY_ID,           // home node replies with IDs of sharers to requestor           done
    INV,                // owner node asks sharers to invalidate                        done
    UPGRADE,            // owner node asks home node to change state to EM              done
    WRITEBACK_INV,      // home node asks owner node to flush and change to INVALID     done
    WRITEBACK_INT,      // home node asks owner node to flush and change to SHARED      done
    FLUSH,              // owner flushes data to home + requestor                       done
    FLUSH_INVACK,       // flush, piggybacking an InvAck message                        done
    EVICT_SHARED,       // handle cache replacement of a shared cache line
    EVICT_MODIFIED      // handle cache replacement of a modified cache line            done
} transactionType;

// We will create our own address space which will be of size 1 byte
// LSB 4 bits indicate the location in memory
// MSB 4 bits indicate the processor it is present in.
// For example, 0x36 means the memory block at index 6 in the 3rd processor
typedef struct instruction {
    byte type;      // 'R' for read, 'W' for write
    byte address;
    byte value;     // used only for write operations
} instruction;

typedef struct cacheLine {
    byte address;           // this is the address in memory
    byte value;             // this is the value stored in cached memory
    cacheLineState state;   // state for you to implement MESI protocol
} cacheLine;

typedef struct directoryEntry {
    byte bitVector;         // each bit indicates whether that processor has this
                            // memory block
    directoryEntryState state;
} directoryEntry;

typedef struct processorNode {
    cacheLine cache[ CACHE_SIZE ];
    byte memory[ MEM_SIZE ];
    directoryEntry directory[ MEM_SIZE ];
    instruction instructions[ MAX_INSTR_NUM ];
    int instructionCount;
} processorNode;

// Note that each message will contain values only in the fields which are relevant 
// to the transactionType
typedef struct message {
    transactionType type;
    int sender;          // thread id that sent the message
    byte address;        // memory block address
    byte value;          // value in memory / cache
    byte bitVector;      // ids of sharer nodes
    int secondReceiver;  // used when you need to send a message to 2 nodes, where
                         // 1 node id is in the sender field
    directoryEntryState dirState;   // directory entry state of the memory block
} message;

typedef struct messageBuffer {
    message queue[ MSG_BUFFER_SIZE ];
    // a circular queue message buffer
    int head;
    int tail;
    int count;
} messageBuffer;

void initializeProcessor( int threadId, processorNode *node, char *dirName );
void sendMessage( int receiver, message msg );
void handleCacheReplacement( int sender, cacheLine oldCacheLine );
void printProcessorState( int processorId, processorNode node );

messageBuffer messageBuffers[ NUM_PROCS ];
omp_lock_t msgBufferLocks[ NUM_PROCS ];

int main( int argc, char * argv[] ) {
    if (argc < 2) {
        fprintf( stderr, "Usage: %s <test_directory>\n", argv[0] );
        return EXIT_FAILURE;
    }
    char *dirName = argv[1];
    
    omp_set_num_threads( NUM_PROCS );

    for ( int i = 0; i < NUM_PROCS; i++ ) {
        messageBuffers[ i ].count = 0;
        messageBuffers[ i ].head = 0;
        messageBuffers[ i ].tail = 0;
        omp_init_lock( &msgBufferLocks[ i ] );
    }
    processorNode node;

    #pragma omp parallel default( none ) private( node ) \
                         shared( messageBuffers, msgBufferLocks, dirName )
    {
        int threadId = omp_get_thread_num();
        initializeProcessor( threadId, &node, dirName );
        // wait for all processors to complete initialization before proceeding
        #pragma omp barrier

        message msg;
        message msgReply;
        instruction instr;
        int instructionIdx = -1;
        int printProcState = 1;         // control how many times to dump processor
        byte waitingForReply = 0;       // if a processor is waiting for a reply
                                        // then dont proceed to next instruction
                                        // as current instruction has not finished
        while ( 1 ) {
            // Process all messages in message queue first
            while ( 
                messageBuffers[ threadId ].count > 0 &&
                messageBuffers[ threadId ].head != messageBuffers[ threadId ].tail
            ) {
                int head = messageBuffers[ threadId ].head;
                msg = messageBuffers[ threadId ].queue[ head ];
                messageBuffers[ threadId ].head = ( head + 1 ) % MSG_BUFFER_SIZE;

                #ifdef DEBUG
                printf( "Processor %d msg from: %d, type: %d, address: 0x%02X\n",
                        threadId, msg.sender, msg.type, msg.address );
                #endif /* ifdef DEBUG */

                byte procNodeAddr = msg.address >> 4;
                byte memBlockAddr = msg.address & ( ( 1 << 4 ) - 1 );
                byte cacheIndex = memBlockAddr % CACHE_SIZE;

                switch ( msg.type ) {
                    case READ_REQUEST:
                        switch ( node.directory[ memBlockAddr ].state ) {
                            case U:
                                node.directory[ memBlockAddr ].state = EM;
                            case S:
                                msgReply.type = REPLY_RD;
                                msgReply.address = msg.address;
                                msgReply.value = node.memory[ memBlockAddr ];
                                msgReply.sender = threadId;
                                msgReply.dirState = 
                                    node.directory[ memBlockAddr ].state;
                                node.directory[ memBlockAddr ].bitVector |=
                                    ( 1 << msg.sender );
                                sendMessage( msg.sender, msgReply );
                                break;
                            case EM:
                                msgReply.type = WRITEBACK_INT;
                                msgReply.address = msg.address;
                                msgReply.secondReceiver = msg.sender;
                                msgReply.sender = threadId;
                                // since the state is EM, only one bit should
                                // be set in the directory bit vectory
                                int receiver = __builtin_ctz(
                                    node.directory[ memBlockAddr ].bitVector );
                                sendMessage( receiver, msgReply );
                                waitingForReply++;
                                break;
                        }
                        break;

                    case REPLY_RD:
                        if ( node.cache[ cacheIndex ].address != 0xFF ) {
                            // some other memory block was in the cache
                            handleCacheReplacement( threadId,
                                                    node.cache[ cacheIndex ] );
                        }
                        node.cache[ cacheIndex ].address = msg.address;
                        node.cache[ cacheIndex ].value = msg.value;
                        if ( msg.dirState == EM ) {
                            node.cache[ cacheIndex ].state = EXCLUSIVE;
                        } else {
                            node.cache[ cacheIndex ].state = SHARED;
                        }
                        waitingForReply--;
                        break;

                    case WRITEBACK_INT:
                        // this is the owner node which contains a cache line in 
                        // MODIFIED state
                        // flush this value to the home node and the requesting node
                        // and change cache line state to SHARED
                        node.cache[ cacheIndex ].state = SHARED;
                        msgReply.type = FLUSH;
                        msgReply.value = node.cache[ cacheIndex ].value;
                        // this is used in the home node to update the bit vector
                        msgReply.secondReceiver = msg.secondReceiver;
                        msgReply.address = msg.address;
                        msgReply.sender = threadId;
                        // send FLUSH to home node
                        sendMessage( msg.sender, msgReply );
                        // send FLUSH to requesting node
                        // if home node is requesting node, dont send again
                        if ( msg.sender != msg.secondReceiver ) {
                            sendMessage( msg.secondReceiver, msgReply );
                        }
                        break;

                    case FLUSH:
                        // if home node, update directory and memory
                        // if requesting node, load block into cache
                        if ( procNodeAddr == threadId ) {
                            node.directory[ memBlockAddr ].state = S;
                            // update the bit vector to include requesting node
                            // NOTE: we are using secondReceiver field to hold the 
                            // requesting node id as explained in WRITEBACK_INT case
                            node.directory[ memBlockAddr ].bitVector |= 
                                ( 1 << msg.secondReceiver );
                            node.memory[ memBlockAddr ] = msg.value;
                            waitingForReply--;
                        }
                        if ( msg.secondReceiver == threadId ) {
                            if ( node.cache[ cacheIndex ].address != 0xFF ) {
                                // some other memory block was in the cache
                                handleCacheReplacement( threadId,
                                                        node.cache[ cacheIndex ] );
                            }
                            node.cache[ cacheIndex ].address = msg.address;
                            node.cache[ cacheIndex ].state = SHARED;
                            node.cache[ cacheIndex ].value = msg.value;
                            waitingForReply--;
                        }
                        break;

                    case UPGRADE:
                        // the requesting node had a write hit
                        // send the bitvector to requesting node, which will be used
                        // to send INV
                        // update directory state to EM, and bit vector to only have
                        // the requesting node set
                        msgReply.type = REPLY_ID;
                        msgReply.sender = threadId;
                        msgReply.address = msg.address;
                        msgReply.value = msg.value;
                        // exclude the requesting node from the sharers list
                        msgReply.bitVector =
                            node.directory[ memBlockAddr ].bitVector & 
                            ~ ( 1 << msg.sender );
                        sendMessage( msg.sender, msgReply );
                        node.directory[ memBlockAddr ].state = EM;
                        node.directory[ memBlockAddr ].bitVector =
                            ( 1 << msg.sender );
                        break;

                    case REPLY_ID:
                        // this is the owner node
                        // got the list of sharers from home node
                        // send INV to all the sharers
                        msgReply.type = INV;
                        msgReply.sender = threadId;
                        msgReply.address = msg.address;
                        for ( int i = 0; i < NUM_PROCS; i++ ) {
                            if ( ( msg.bitVector >> i ) & 1 ) {
                                sendMessage( i, msgReply );
                            }
                        }
                        // NOTE: Ideally, we should update the owner node cache line
                        // AFTER we receive INVACK from every sharer, but for that
                        // we will have to keep track of all the INVACKS.
                        // Instead, we will assume that INV does not fail.
                        if ( node.cache[ cacheIndex ].address != 0xFF ) {
                            // some other memory block was in the cache
                            handleCacheReplacement( threadId,
                                                    node.cache[ cacheIndex ] );
                        }
                        node.cache[ cacheIndex ].state = MODIFIED;
                        node.cache[ cacheIndex ].value = msg.value;
                        node.cache[ cacheIndex ].address = msg.address;
                        waitingForReply--;
                        break;

                    case INV:
                        // invalidate the cache entry for memory block
                        // if the cache no longer has the memory block ( replaced by
                        // a different block ), then do nothing
                        if ( node.cache[ cacheIndex ].address == msg.address ) {
                            node.cache[ cacheIndex ].state = INVALID;
                        }
                        break;

                    case WRITE_REQUEST:
                        // this is in the home node
                        // write miss occured in requesting node
                        switch( node.directory[ memBlockAddr ].state ) {
                            case U:
                                // no cache contains this memory block
                                // requesting node directly becomes the owner node
                                msgReply.type = REPLY_WR;
                                msgReply.address = msg.address; 
                                msgReply.value = msg.value;
                                msgReply.sender = threadId;
                                node.directory[ memBlockAddr ].state = EM;
                                node.directory[ memBlockAddr ].bitVector =
                                    ( 1 << msg.sender );
                                sendMessage( msg.sender, msgReply );
                                break;
                            case S:
                                // other caches contain this memory block, but it is
                                // not modified
                                // invalidate the entries in other caches
                                msgReply.type = INV;
                                msgReply.sender = threadId;
                                msgReply.address = msg.address;
                                msgReply.value = msg.value;
                                byte sharers =
                                    node.directory[ memBlockAddr ].bitVector;
                                for ( int i = 0; i < NUM_PROCS; i++ ) {
                                    if ( ( ( sharers >> i ) & 1 ) &&
                                         ( i != msg.sender ) ) {
                                        sendMessage( i, msgReply );
                                    }
                                }
                                // NOTE: we are not waiting for INVACKs from the
                                // sharers, explained in REPLY_ID case
                                msgReply.type = REPLY_WR;
                                sendMessage( msg.sender, msgReply );
                                // update the directory state to EM, and bitvector
                                // to include only the requesting node
                                node.directory[ memBlockAddr ].state = EM;
                                node.directory[ memBlockAddr ].bitVector =
                                    ( 1 << msg.sender );
                                break;
                            case EM:
                                // one other cache contains this memory block, which
                                // can be modified
                                // send a WRITEBACK_INV to the owner node
                                msgReply.type = WRITEBACK_INV;
                                msgReply.address = msg.address;
                                msgReply.sender = threadId;
                                msgReply.value = msg.value;
                                msgReply.secondReceiver = msg.sender;
                                // since the state is EM, only one bit should
                                // be set in the directory bit vectory
                                int receiver = __builtin_ctz(
                                    node.directory[ memBlockAddr ].bitVector );
                                sendMessage( receiver, msgReply );
                                waitingForReply--;
                                break;
                        }
                        break;

                    case REPLY_WR:
                        if ( node.cache[ cacheIndex ].address != 0xFF ) {
                            // some other memory block was in the cache
                            handleCacheReplacement( threadId,
                                                    node.cache[ cacheIndex ] );
                        }
                        node.cache[ cacheIndex ].address = msg.address;
                        node.cache[ cacheIndex ].value = msg.value;
                        node.cache[ cacheIndex ].state = MODIFIED;
                        waitingForReply--;
                        break;

                    case WRITEBACK_INV:
                        // this is in owner node
                        // flush the currrent value to home node
                        msgReply.type = FLUSH_INVACK;
                        msgReply.value = node.cache[ cacheIndex ].value;
                        msgReply.address = msg.address;
                        msgReply.secondReceiver = msg.secondReceiver;
                        sendMessage( msg.sender, msgReply );
                        // send an ack to the requesting node which will become the
                        // new owner node
                        // if home node is the new owner node, dont send again
                        if ( msg.secondReceiver != msg.sender ) {
                            sendMessage( msg.secondReceiver, msgReply );
                        }
                        // invalidate the cache line
                        node.cache[ cacheIndex ].state = INVALID;
                        break;

                    case FLUSH_INVACK:
                        // if home node, update directory and memory
                        // if requesting node, load block into cache
                        if ( procNodeAddr == threadId ) {
                            node.directory[ memBlockAddr ].state = EM;
                            // update the bit vector to only have requesting node
                            // which will become the new owner node
                            node.directory[ memBlockAddr ].bitVector = 
                                ( 1 << msg.secondReceiver );
                            node.memory[ memBlockAddr ] = msg.value;
                            waitingForReply--;
                        } 
                        if ( msg.secondReceiver == threadId ) {
                            if ( node.cache[ cacheIndex ].address != 0xFF ) {
                                // some other memory block was in the cache
                                handleCacheReplacement( threadId,
                                                        node.cache[ cacheIndex ] );
                            }
                            node.cache[ cacheIndex ].address = msg.address;
                            node.cache[ cacheIndex ].state = MODIFIED;
                            node.cache[ cacheIndex ].value = instr.value;
                            waitingForReply--;
                        }
                        break;
                    
                    case EVICT_SHARED:
                        // in home node, remove the old node from bitvector
                        if ( procNodeAddr == threadId ) {
                            node.directory[ memBlockAddr ].bitVector &=
                                ~ ( 1 << msg.sender );
                            // if no more sharers exist, change directory state to U
                            // if only one sharer exist, change directory state to EM
                            byte bitVec = node.directory[ memBlockAddr ].bitVector;
                            if ( bitVec == 0 ) {
                                node.directory[ memBlockAddr ].state = U;
                            } else if ( bitVec && !( bitVec & ( bitVec - 1 ) ) ) {
                                node.directory[ memBlockAddr ].state = EM;
                                // inform owner node to change from SHARED to
                                // EXCLUSIVE
                                int receiver = __builtin_ctz(
                                    node.directory[ memBlockAddr ].bitVector );
                                if ( receiver == threadId ) {
                                    // home node is same as owner node
                                    node.cache[ cacheIndex ].state = EXCLUSIVE;
                                } else {
                                    msgReply.type = EVICT_SHARED;
                                    msgReply.sender = threadId;
                                    sendMessage( receiver, msgReply );
                                }
                            }
                        } else {
                            // update cacheline from SHARED to EXCLUSIVE
                            node.cache[ cacheIndex ].state = EXCLUSIVE;
                        }
                        break;

                    case EVICT_MODIFIED:
                        // flush value to memory
                        node.memory[ memBlockAddr ] = msg.value;
                        // in home node, remove the old node from bitvector
                        // since it was in MODIFIED state, no other node should have
                        // had that memory block in a valid state in its cache
                        node.directory[ memBlockAddr ].bitVector = 0;
                        node.directory[ memBlockAddr ].state = U;
                        break;
                }
            }
            
            // Check if we are waiting for a reply from a different node
            // if yes, then we have to complete the previous instruction before
            // moving on to the next
            if ( waitingForReply > 0 ) {
                continue;
            }
            // Process an instruction
            if ( instructionIdx < node.instructionCount - 1 ) {
                instructionIdx++;
            } else {
                if ( printProcState > 0 ) {
                    printProcessorState( threadId, node );
                    printProcState--;
                }
                // even though there are no more instructions, this processor might
                // still need to react to new transaction messages
                continue;
            }
            instr = node.instructions[ instructionIdx ];
            #ifdef DEBUG
            printf( "Processor %d: instr type=%c, address=0x%02X, value=%hhu\n",
                    threadId, instr.type, instr.address, instr.value );
            #endif
            byte procNodeAddr = instr.address >> 4;
            byte memBlockAddr = instr.address & ( ( 1 << 4 ) - 1 );
            byte cacheIndex = memBlockAddr % CACHE_SIZE;

          if ( instr.type == 'R' ) {
                //  check if memory block is present in cache
                //  if cache hit and the cache line state is not invalid, then use
                //  that value. no transaction/transition takes place so no work.
                //  if cache line is invalid, it is treated as a read miss
                if ( node.cache[ cacheIndex ].address != instr.address ||
                     node.cache[ cacheIndex ].state == INVALID ) {
                    // handle read miss
                    // send a READ_REQUEST to home node
                    msg.type = READ_REQUEST;
                    msg.address = instr.address;
                    msg.sender = threadId;
                    sendMessage( procNodeAddr, msg );
                    waitingForReply++;
                }
            } else {
                // write hit on an invalid cache line is treated as write miss
                if ( node.cache[ cacheIndex ].address == instr.address &&
                     node.cache[ cacheIndex ].state != INVALID ) {
                    // handle write hits
                    switch ( node.cache[ cacheIndex ].state ) {
                        case MODIFIED:
                        case EXCLUSIVE:
                            // only this node contains the memory block
                            // no network transactions required
                            node.cache[ cacheIndex ].state = MODIFIED;
                            node.cache[ cacheIndex ].value = instr.value;
                            break;
                        case SHARED:
                            // other nodes contain this memory block
                            // request home node to send list of sharers
                            msg.type = UPGRADE;
                            msg.address = instr.address;
                            msg.sender = threadId;
                            msg.value = instr.value;
                            sendMessage( procNodeAddr, msg );
                            waitingForReply++;
                            break;
                        case INVALID:
                            // handled in write miss
                            break;
                    }
                } else {
                    // handle write misses
                    // send a WRITE_REQUEST to home node
                    msg.type = WRITE_REQUEST;
                    msg.address = instr.address;
                    msg.sender = threadId;
                    msg.value = instr.value;
                    sendMessage( procNodeAddr, msg );
                    waitingForReply++;
                }
            }

        }
    }
}

void initializeProcessor( int threadId, processorNode *node, char *dirName ) {
    for ( int i = 0; i < MEM_SIZE; i++ ) {
        node->memory[ i ] = 20 * threadId + i;  // some initial value to mem block
        node->directory[ i ].bitVector = 0;     // no cache has this block at start
        node->directory[ i ].state = U;         // this block is in Unowned state
    }

    for ( int i = 0; i < CACHE_SIZE; i++ ) {
        node->cache[ i ].address = 0xFF;        // this address is invalid as we can
                                                // have a maximum of 8 nodes in the 
                                                // current implementation
        node->cache[ i ].value = 0;
        node->cache[ i ].state = INVALID;       // all cache lines are invalid
    }

    // read and parse instructions from core_<threadId>.txt
    char filename[ 128 ];
    snprintf(filename, sizeof(filename), "tests/%s/core_%d.txt", dirName, threadId);
    FILE *file = fopen( filename, "r" );
    if ( !file ) {
        fprintf( stderr, "Error: count not open file %s\n", filename );
        exit( EXIT_FAILURE );
    }

    char line[ 20 ];
    node->instructionCount = 0;
    while ( fgets( line, sizeof( line ), file ) &&
            node->instructionCount < MAX_INSTR_NUM ) {
        if ( line[ 0 ] == 'R' && line[ 1 ] == 'D' ) {
            sscanf( line, "RD %hhx",
                    &node->instructions[ node->instructionCount ].address );
            node->instructions[ node->instructionCount ].type = 'R';
            node->instructions[ node->instructionCount ].value = 0;
        } else if ( line[ 0 ] == 'W' && line[ 1 ] == 'R' ) {
            sscanf( line, "WR %hhx %hhu",
                    &node->instructions[ node->instructionCount ].address,
                    &node->instructions[ node->instructionCount ].value );
            node->instructions[ node->instructionCount ].type = 'W';
        }
        node->instructionCount++;
    }

    fclose( file );
    #ifdef DEBUG
    printf( "Processor %d initialized\n", threadId );
    #endif /* ifdef DEBUG */
}

void sendMessage( int receiver, message msg ) {
    omp_set_lock( &msgBufferLocks[ receiver ] );
    int tail = messageBuffers[ receiver ].tail;
    messageBuffers[ receiver ].queue[ tail ] = msg;
    messageBuffers[ receiver ].tail = ( tail + 1 ) % MSG_BUFFER_SIZE;
    messageBuffers[ receiver ].count++;
    omp_unset_lock( &msgBufferLocks[ receiver ] );
}

void handleCacheReplacement( int sender, cacheLine oldCacheLine ) {
    message msg;
    msg.sender = sender;
    msg.address = oldCacheLine.address;
    byte procNodeAddr = msg.address >> 4;
    byte memBlockAddr = msg.address & ( ( 1 << 4 ) - 1 );
    switch ( oldCacheLine.state ) {
        case EXCLUSIVE:
        case SHARED:
            // update directory in home node
            msg.type = EVICT_SHARED;
            sendMessage( procNodeAddr, msg );
            break;
        case MODIFIED:
            // update directory and flush memory in home node
            msg.type = EVICT_MODIFIED;
            msg.value = oldCacheLine.value;
            sendMessage( procNodeAddr, msg );
            break;
        case INVALID:
            // do nothing if in INVALID
            break;
    }
}

void printProcessorState(int processorId, processorNode node) {
    static const char *cacheStateStr[] = { "MODIFIED", "EXCLUSIVE", "SHARED",
                                           "INVALID" };
    static const char *dirStateStr[] = { "EM", "S", "U" };

    #pragma omp critical
    {
    printf("=======================================\n");
    printf(" Processor Node: %d\n", processorId);
    printf("=======================================\n\n");

    // Print memory state
    printf("-------- Memory State -------\n");
    printf("| Index | Address | Value   |\n");
    printf("|---------------------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        printf("|  %3d  |  0x%02X   |  %5d  |\n", i, ( processorId << 4 ) + i,
                node.memory[i]);
    }
    printf("-----------------------------\n\n");

    // Print directory state
    printf("------------ Directory State --------------\n");
    printf("| Index | Address | State | BitVector     |\n");
    printf("|-----------------------------------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        printf("|  %3d  |  0x%02X   |  %2s  |   0x%08B   |\n", i,
                ( processorId << 4 ) + i, dirStateStr[node.directory[i].state],
                node.directory[i].bitVector);
    }
    printf("-------------------------------------------\n\n");
    
    // Print cache state
    printf("------------ Cache State ----------------\n");
    printf("| Index | Address | Value |  State      |\n");
    printf("|---------------------------------------|\n");
    for (int i = 0; i < CACHE_SIZE; i++) {
        printf("|  %3d  |  0x%02X   |  %3d  |  %8s \t|\n", 
               i, node.cache[i].address, node.cache[i].value,
               cacheStateStr[node.cache[i].state]);
    }
    printf("----------------------------------------\n\n");
    }
}
