//
//  BREthereumLES.h
//  breadwallet-core Ethereum
//
//  Created by Lamont Samuels on 5/01/18.
//  Copyright (c) 2018 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include "BRRlpCoder.h"
#include "BREthereumLES.h"
#include "BREthereumLESCoder.h"
#include "BREthereumBase.h"
#include "BREthereumTransaction.h"
#include "BREthereumNetwork.h"
#include "BRArray.h"
#include "BREthereumNodeManager.h"
#include "BRRlp.h"
#include "BRUtil.h"

#define ETH_LOG_TOPIC "BREthereumLES"

typedef struct {
  LESMessageId messageId;
  uint64_t requestId;
  union
  {
     struct {
         BREthereumLESTransactionStatusCallback callback;
         BREthereumLESTransactionStatusContext ctx;
         BREthereumHash* transaction;
         size_t transactionCount;
     }transaction_status;

  }u;
}LESRequestRecord;

struct BREthereumLESContext {
    BREthereumNodeManager nodeManager;
    BRKey* key;
    BREthereumNetwork network;
    BREthereumLESStatusMessage peerStatus;
    BREthereumLESStatusMessage statusMsg;
    BREthereumBoolean startSendingMessages;
    BREthereumSubProtoCallbacks callbacks;
    BREthereumLESStatus status;
    LESRequestRecord* requests;
    uint64_t message_id_offset;
    uint64_t requestIdCount;
    pthread_mutex_t lock;
};
static void _receivedMessageCallback(BREthereumSubProtoContext info, uint8_t* message, size_t messageSize) {

    BREthereumLES les = (BREthereumLES)info;
    BRRlpCoder coder = rlpCoderCreate();
    BRRlpData data = {messageSize, message};
    BRRlpItem item = rlpGetItem (coder, data);
    
    //Set default values for optional status values
    size_t itemsCount;
    const BRRlpItem *items = rlpDecodeList(coder, item, &itemsCount);

    uint64_t messageId = rlpDecodeItemUInt64(coder, items[0],0);
    
    switch (messageId)
    {
        case BRE_LES_ID_STATUS:
        {
            BREthereumLESDecodeStatus remoteStatus = ethereumLESDecodeStatus(message, messageSize, &les->peerStatus);
            eth_log(ETH_LOG_TOPIC, "%s", "Received Status message from Remote peer");
            les->startSendingMessages = ETHEREUM_BOOLEAN_TRUE;
        }
        case BRE_LES_ID_TX_STATUS:
        {
            uint64_t reqId = -1, bv = -1;
            BREthereumTransactionStatusLES* replies;
            size_t repliesCount;
            BREthereumLESDecodeStatus status = ethereumLESDecodeTxStatus(message, messageSize, &reqId, &bv, &replies, &repliesCount);
            int requestIndexRm = 0;
            pthread_mutex_lock(&les->lock);
            for(int i = 0; i < array_count(les->requests); i++) {
                if(reqId == les->requests[i].requestId){
                    requestIndexRm = i;
                    break;
                }
            }
            pthread_mutex_unlock(&les->lock);
            
            for(int i = 0; i < repliesCount; ++i){
                les->requests[requestIndexRm].u.transaction_status.callback(les->requests[requestIndexRm].u.transaction_status.ctx,
                                                                            les->requests[requestIndexRm].u.transaction_status.transaction[i],
                                                                            replies[i]);
            }
            pthread_mutex_lock(&les->lock);
            array_rm(les->requests, requestIndexRm);
            pthread_mutex_unlock(&les->lock);
            eth_log(ETH_LOG_TOPIC, "%s", "Received Tranactions Status message from Remote peer");
        }
        break;
        default:
        {
            eth_log(ETH_LOG_TOPIC, "Received Message that cannot be handled, message id:%llu", messageId);
        }
        break;
    }
    rlpCoderRelease(coder);
}

static void _connectedToNetworkCallback(BREthereumSubProtoContext info, uint8_t** statusBytes, size_t* statusSize){
    BREthereumLES les = (BREthereumLES)info;
    BRRlpData statusPayload = ethereumLESEncodeStatus(les->message_id_offset, &les->statusMsg);
    *statusBytes = statusPayload.bytes;
    *statusSize = statusPayload.bytesCount;
}
static void _networkReachableCallback(BREthereumSubProtoContext info, BREthereumBoolean isReachable) {}

static BREthereumLESStatus _sendMessage(BREthereumLES les, uint8_t packetType, BRRlpData payload) {

    BREthereumNodeManagerStatus status = ethereumNodeManagerStatus(les->nodeManager);
    BREthereumLESStatus retStatus = LES_NETWORK_UNREACHABLE;
    
    if(ETHEREUM_BOOLEAN_IS_TRUE(les->startSendingMessages)){
      
       if(status == BRE_MANAGER_CONNECTED)
        {
            if(ETHEREUM_BOOLEAN_IS_TRUE(ethereumNodeManagerSendMessage(les->nodeManager, packetType, payload.bytes, payload.bytesCount))){
                retStatus = LES_SUCCESS;
            }
        }
        else
        {
            //Retry to connect to the ethereum network
            if(ethereumNodeMangerConnect(les->nodeManager)){
                sleep(3); //Give it some time to see if the node manager can connect to the network again;
                if(ETHEREUM_BOOLEAN_IS_TRUE(ethereumNodeManagerSendMessage(les->nodeManager, packetType, payload.bytes, payload.bytesCount))){
                    retStatus = LES_SUCCESS;
                }
            }
        }
    }
    return retStatus;
}
//
// Public functions
//
extern BREthereumLES
lesCreate (BREthereumNetwork network,
           BREthereumLESAnnounceContext announceContext,
           BREthereumLESAnnounceCallback announceCallback,
           BREthereumHash headHash,
           uint64_t headNumber,
           uint64_t headTotalDifficulty,
           BREthereumHash genesisHash) {
    
    BREthereumLES les = (BREthereumLES) calloc(1,sizeof(struct BREthereumLESContext));
    
    if(les != NULL)
    {
        les->requestIdCount = 0;
        les->callbacks.info = les;
        les->callbacks.connectedFunc = _connectedToNetworkCallback;
        les->callbacks.messageRecFunc = _receivedMessageCallback;
        les->callbacks.networkReachFunc = _networkReachableCallback;
        les->key = (BRKey *)malloc(sizeof(BRKey));
        uint8_t data[32] = {3,4,5,0};
        memcpy(les->key->secret.u8,data,32);
        les->key->compressed = 0;
        les->startSendingMessages = ETHEREUM_BOOLEAN_TRUE;
        les->network = network;
        //Define the status message
        les->statusMsg.protocolVersion = 0x02;
        les->statusMsg.chainId = networkGetChainId(network);
        memcpy(les->statusMsg.headHash, headHash.bytes, 32);
        les->statusMsg.headNum = headNumber;
        memcpy(les->statusMsg.genesisHash, genesisHash.bytes, 32);
        les->statusMsg.serveHeaders = ETHEREUM_BOOLEAN_FALSE;
        les->statusMsg.serveChainSince = NULL;
        les->statusMsg.serveStateSince = NULL;
        les->statusMsg.txRelay = ETHEREUM_BOOLEAN_FALSE;
        les->statusMsg.flowControlBL = NULL;
        les->statusMsg.flowControlMRC = NULL;
        les->statusMsg.flowControlMRR = NULL;
        les->statusMsg.flowControlMRCCount = NULL;
        les->statusMsg.announceType = 0;

        //Assign the message id offset for now to be 0x10 since we only support LES
        les->message_id_offset = 0x10;

        //Define the Requests Array
        array_new(les->requests,100);

        les->nodeManager = ethereumNodeManagerCreate(network, les->key, headHash, headNumber, headTotalDifficulty, genesisHash, les->callbacks);
        pthread_mutex_init(&les->lock, NULL);
        ethereumNodeMangerConnect(les->nodeManager);
        
    }
    return les;
}
extern void lesRelease(BREthereumLES les) {
    ethereumNodeManagerDisconnect(les->nodeManager); 
    ethereumNodeManagerRelease(les->nodeManager);
    free(les);
}
//
// LES messages
//
extern BREthereumLESStatus
lesGetTransactionStatus (BREthereumLES les,
                         BREthereumLESTransactionStatusContext context,
                         BREthereumLESTransactionStatusCallback callback,
                         BREthereumHash transactions[]) {
                         
    BREthereumBoolean shouldSend;
    pthread_mutex_lock(&les->lock);
    shouldSend = les->startSendingMessages;
    uint64_t reqId = les->requestIdCount++;
    pthread_mutex_unlock(&les->lock);
    if(ETHEREUM_BOOLEAN_IS_TRUE(shouldSend))
    {
        LESRequestRecord record;
        record.messageId = BRE_LES_ID_GET_TX_STATUS;
        record.requestId = reqId;
        record.u.transaction_status.callback = callback;
        record.u.transaction_status.ctx = context;
        record.u.transaction_status.transaction = malloc(sizeof(BREthereumHash) * array_count(transactions));
        record.u.transaction_status.transactionCount = array_count(transactions);
        for(int i = 0; i < array_count(transactions); ++i) {
            memcpy(record.u.transaction_status.transaction[i].bytes, transactions[i].bytes, sizeof(transactions[0].bytes));
        }
        BRRlpData txtStatusData = ethereumLESGetTxStatus(les->message_id_offset, reqId, transactions);
        BREthereumLESStatus status =  _sendMessage(les, BRE_LES_ID_GET_TX_STATUS, txtStatusData);
        rlpDataRelease(txtStatusData);
        return status;
    }
    else {
        return LES_NETWORK_UNREACHABLE;
    }
}

extern BREthereumLESStatus
lesGetTransactionStatusOne (BREthereumLES les,
                            BREthereumLESTransactionStatusContext context,
                            BREthereumLESTransactionStatusCallback callback,
                            BREthereumHash transaction) {
    
    BREthereumBoolean shouldSend;

    pthread_mutex_lock(&les->lock);
    uint64_t reqId = les->requestIdCount++;
    shouldSend = les->startSendingMessages;
    pthread_mutex_unlock(&les->lock);

    if(ETHEREUM_BOOLEAN_IS_TRUE(shouldSend)) {
        LESRequestRecord record;
        record.messageId = BRE_LES_ID_GET_TX_STATUS;
        record.requestId = reqId;
        record.u.transaction_status.callback = callback;
        record.u.transaction_status.ctx = context;
        record.u.transaction_status.transaction = malloc(sizeof(BREthereumHash));
        memcpy(record.u.transaction_status.transaction->bytes, transaction.bytes, sizeof(transaction.bytes));
        record.u.transaction_status.transactionCount = 1;
        
        BREthereumHash* transactionArr;
        array_new(transactionArr, 1);
        array_add(transactionArr, transaction);
        
        BRRlpData txtStatusData = ethereumLESGetTxStatus(les->message_id_offset, reqId, transactionArr);
        BREthereumLESStatus status =  _sendMessage(les, BRE_LES_ID_GET_TX_STATUS, txtStatusData);
        rlpDataRelease(txtStatusData);
        array_free(transactionArr);
        return status;
    }else {
        return LES_NETWORK_UNREACHABLE; 
    }
}

extern BREthereumLESStatus
lesSubmitTransaction (BREthereumLES les,
                      BREthereumLESTransactionStatusContext context,
                      BREthereumLESTransactionStatusCallback callback,
                      BREthereumTransactionRLPType type,
                      BREthereumTransaction transaction) {
    
    BREthereumBoolean shouldSend;

    pthread_mutex_lock(&les->lock);
    uint64_t reqId = les->requestIdCount++;
    shouldSend = les->startSendingMessages;
    pthread_mutex_unlock(&les->lock);
    
    if(ETHEREUM_BOOLEAN_IS_TRUE(shouldSend)) {
        BREthereumTransaction* transactionArr;
        array_new(transactionArr, 1);
        array_add(transactionArr, transaction);
        BRRlpData sendTxtData = ethereumLESSendTxt(les->message_id_offset, reqId, transactionArr, les->network, type);
        BREthereumLESStatus status =  _sendMessage(les, BRE_LES_ID_GET_TX_STATUS, sendTxtData);
        array_free(transactionArr);
        rlpDataRelease(sendTxtData);
        return status;
    }
    else {
        return LES_NETWORK_UNREACHABLE;
    }
}
extern BREthereumLESStatus
lesGetReceipts (BREthereumLES les,
                BREthereumLESReceiptsContext context,
                BREthereumLESReceiptsCallback callback,
                BREthereumHash blocks[]) {
    
    return LES_UNKNOWN_ERROR;
}

extern BREthereumLESStatus
lesGetReceiptsOne (BREthereumLES les,
                   BREthereumLESReceiptsContext context,
                   BREthereumLESReceiptsCallback callback,
                   BREthereumHash block) {
    
    return LES_UNKNOWN_ERROR;
}


extern BREthereumLESStatus
lesGetBlockBodies (BREthereumLES les,
                   BREthereumLESBlockBodiesContext context,
                   BREthereumLESBlockBodiesCallback callback,
                   BREthereumHash blocks[]) {
    
    return LES_UNKNOWN_ERROR;
}

extern BREthereumLESStatus
lesGetBlockBodiesOne (BREthereumLES les,
                      BREthereumLESBlockBodiesContext context,
                      BREthereumLESBlockBodiesCallback callback,
                      BREthereumHash block) {
    
    return LES_UNKNOWN_ERROR;
}
