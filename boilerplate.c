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
    READ_REQUEST,       // requesting node sends to home node on a read miss 
    WRITE_REQUEST,      // requesting node sends to home node on a write miss 
    REPLY_RD,           // home node replies with data to requestor for read request
    REPLY_WR,           // home node replies to requestor for write request
    REPLY_ID,           // home node replies with IDs of sharers to requestor
    INV,                // owner node asks sharers to invalidate
    UPGRADE,            // owner node asks home node to change state to EM
    WRITEBACK_INV,      // home node asks owner node to flush and change to INVALID
    WRITEBACK_INT,      // home node asks owner node to flush and change to SHARED
    FLUSH,              // owner flushes data to home + requestor
    FLUSH_INVACK,       // flush, piggybacking an InvAck message
    EVICT_SHARED,       // handle cache replacement of a shared cache line
    EVICT_MODIFIED      // handle cache replacement of a modified cache line
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
                            // memory block in its cache
    directoryEntryState state;
} directoryEntry;

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

typedef struct processorNode {
    cacheLine cache[ CACHE_SIZE ];
    byte memory[ MEM_SIZE ];
    directoryEntry directory[ MEM_SIZE ];
    instruction instructions[ MAX_INSTR_NUM ];
    int instructionCount;
} processorNode;

void initializeProcessor( int threadId, processorNode *node, char *dirName );
void sendMessage( int receiver, message msg );  // IMPLEMENT
void handleCacheReplacement( int sender, cacheLine oldCacheLine );  // IMPLEMENT
void printProcessorState( int processorId, processorNode node );

messageBuffer messageBuffers[ NUM_PROCS ];
// IMPLEMENT
// Create locks to ensure thread-safe access to each processor's message buffer.
msgBufferLocks[ NUM_PROCS ];

int main( int argc, char * argv[] ) {
    if (argc < 2) {
        fprintf( stderr, "Usage: %s <test_directory>\n", argv[0] );
        return EXIT_FAILURE;
    }
    char *dirName = argv[1];
    
    // IMPLEMENT
    // set number of threads to NUM_PROCS

    for ( int i = 0; i < NUM_PROCS; i++ ) {
        messageBuffers[ i ].count = 0;
        messageBuffers[ i ].head = 0;
        messageBuffers[ i ].tail = 0;
        // IMPLEMENT
        // initialize the locks in msgBufferLocks
    }
    processorNode node;

    // IMPLEMENT
    // Create the omp parallel region with an appropriate data environment
    {
        int threadId = omp_get_thread_num();
        initializeProcessor( threadId, &node, dirName );
        // IMPLEMENT
        // wait for all processors to complete initialization before proceeding

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

                // IMPLEMENT
                // extract procNodeAddr and memBlockAddr from message address
                byte procNodeAddr = ;
                byte memBlockAddr = ;
                byte cacheIndex = memBlockAddr % CACHE_SIZE;

                switch ( msg.type ) {
                    case READ_REQUEST:
                        // IMPLEMENT
                        // This is in the home node
                        // If directory state is,
                        // U: update directory and send value using REPLY_RD
                        // S: update directory and send value using REPLY_RD
                        // EM: forward request to the current owner node for
                        //     writeback intervention using WRITEBACK_INT
                        break;

                    case REPLY_RD:
                        // IMPLEMENT
                        // This is in the requesting node
                        // If some other memory block was in cacheline, you need to
                        // handle cache replacement
                        // Read in the memory block sent in the message to cache
                        // Handle state of the memory block appropriately
                        break;

                    case WRITEBACK_INT:
                        // IMPLEMENT
                        // This is in the owner node which contains a cache line in 
                        // MODIFIED state
                        // Flush this value to the home node and the requesting node
                        // using FLUSH
                        // Change cacheline state to SHARED
                        // If home node is the requesting node, avoid sending FLUSH
                        // twice
                        break;

                    case FLUSH:
                        // IMPLEMENT
                        // If in home node,
                        // update directory state and bitvector appropriately
                        // update memory value
                        //
                        // If in requesting node, load block into cache
                        // If some other memory block was in cacheline, you need to
                        // handle cache replacement
                        //
                        // IMPORTANT: there can be cases where home node is same as
                        // requesting node, which need to be handled appropriately
                        break;

                    case UPGRADE:
                        // IMPLEMENT
                        // This is in the home node
                        // The requesting node had a write hit on a SHARED cacheline
                        // Send list of sharers to requesting node using REPLY_ID
                        // Update directory state to EM, and bit vector to only have
                        // the requesting node set
                        // IMPORTANT: Do not include the requesting node in the
                        // sharers list
                        break;

                    case REPLY_ID:
                        // IMPLEMENT
                        // This is in the owner node
                        // The owner node recevied the sharers list from home node
                        // Invalidate all sharers' entries using INV
                        // Handle cache replacement if needed, and load the block
                        // into the cacheline
                        // NOTE: Ideally, we should update the owner node cache line
                        // after we receive INV_ACK from every sharer, but for that
                        // we will have to keep track of all the INV_ACKs.
                        // Instead, we will assume that INV does not fail.
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
                                msgReply.type = EVICT_SHARED;
                                msgReply.sender = threadId;
                                sendMessage( receiver, msgReply );
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
            
            // Check if we are waiting for a reply message
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

            // IMPLEMENT
            // Extract the home node's address and memory block index from
            // instruction address
            byte procNodeAddr = ;
            byte memBlockAddr = ;
            byte cacheIndex = memBlockAddr % CACHE_SIZE;

          if ( instr.type == 'R' ) {
                // IMPLEMENT
                // check if memory block is present in cache
                //
                // if cache hit and the cacheline state is not invalid, then use
                // that value. no transaction/transition takes place so no work.
                //
                // if cacheline is invalid, or memory block is not present, it is
                // treated as a read miss
                // send a READ_REQUEST to home node on a read miss
            } else {
                // IMPLEMENT
                // check if memory block is present in cache
                //
                // if cache hit and cacheline state is not invalid, then it is a
                // write hit
                // if modified or exclusive, update cache directly as only this node
                // contains the memory block, no network transactions required
                // if shared, other nodes contain this memory block, request home
                // node to send list of sharers. This node will have to send an
                // UPGRADE to home node to promote directory from S to EM, and also
                // invalidate the entries in the sharers
                //
                // if cache miss or cacheline state is invalid, then it is a write
                // miss
                // send a WRITE_REQUEST to home node on a write miss
            }

        }
    }
}

void initializeProcessor( int threadId, processorNode *node, char *dirName ) {
    // IMPORTANT: DO NOT MODIFY
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
    // IMPLEMENT
    // Ensure thread safety while adding a message to the receiver's buffer
    // Manage buffer indices correctly to maintain a circular queue structure
}

void handleCacheReplacement( int sender, cacheLine oldCacheLine ) {
    // IMPLEMENT
    // Notify the home node before a cacheline gets replaced
    // Extract the home node's address and memory block index from cacheline address
    byte memBlockAddr = ;
    byte procNodeAddr = ;
    
    switch ( oldCacheLine.state ) {
        case SHARED:
            // If cache line was shared, inform home node about the eviction
            break;
        case MODIFIED:
            // If cache line was modified, send updated value to home node 
            // so that memory can be updated before eviction.
            break;
        default:
            // No action required for INVALID or EXCLUSIVE states
            break;
    }
}

void printProcessorState(int processorId, processorNode node) {
    // IMPORTANT: DO NOT MODIFY
    static const char *cacheStateStr[] = { "MODIFIED", "EXCLUSIVE", "SHARED",
                                           "INVALID" };
    static const char *dirStateStr[] = { "EM", "S", "U" };

    #pragma omp critical
    {
    printf("=======================================\n");
    printf(" Processor Node: %d\n", processorId);
    printf("=======================================\n\n");

    // Print memory state
    printf("----------- Memory State -----------\n");
    printf("| Index |  Value  |\n");
    printf("|-----------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        printf("|  %3d  |  %5d  |\n", i, node.memory[i]);
    }
    printf("------------------------------------\n\n");

    // Print directory state
    printf("---------- Directory State ---------\n");
    printf("| Index | State | BitVector      |\n");
    printf("|--------------------------------|\n");
    for (int i = 0; i < MEM_SIZE; i++) {
        printf("|  %3d  |   %2s  |   0x%08B   |\n", 
               i, dirStateStr[node.directory[i].state], node.directory[i].bitVector);
    }
    printf("------------------------------------\n\n");

    // Print cache state
    printf("------------ Cache State ----------------\n");
    printf("| Index | Address | Value |  State      |\n");
    printf("|---------------------------------------|\n");
    for (int i = 0; i < CACHE_SIZE; i++) {
        printf("|  %3d  |  0x%02X  |  %3d  |  %8s \t|\n", 
               i, node.cache[i].address, node.cache[i].value,
               cacheStateStr[node.cache[i].state]);
    }
    printf("----------------------------------------\n\n");
    }
}
