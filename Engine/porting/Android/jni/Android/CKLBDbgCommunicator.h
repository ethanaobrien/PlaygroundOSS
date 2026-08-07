/*
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#pragma once

#include "CKLBDebugger.h"

#include <netinet/in.h>
#include <sys/socket.h>

class CKLBDbgCommunicator : public IKLBDebuggerCommunicator
{
public:
    CKLBDbgCommunicator();

    static CKLBDbgCommunicator& getInstance();

    bool process();

    virtual void assignDebugger(CKLBDebuggerContext* debugger);
    virtual u8* allocateResult(u32 commandID, u32 size);
    virtual void sendResult(u32 commandID, CKLBDebuggerContext::ERROR_TYPE error);
    virtual ~CKLBDbgCommunicator();

private:
    bool setupSocket();
    bool acceptClient();
    bool receiveCommand();

    enum ConnectionState {
        STATE_CLOSED,
        STATE_LISTENING,
        STATE_CONNECTED
    };

    enum ReceiveState {
        RECEIVE_HEADER,
        RECEIVE_BODY
    };

    ConnectionState        m_connectionState;
    ReceiveState           m_receiveState;
    sockaddr_in            m_peerAddress;
    socklen_t              m_peerAddressSize;
    int                    m_serverSocket;
    int                    m_clientSocket;
    CKLBDebuggerContext*   m_debugger;
    ssize_t                m_headerBytesRead;
    u8                     m_header[9];
    u32                    m_commandID;
    u32                    m_commandType;
    s32                    m_commandSize;
    u8*                    m_commandData;
    ssize_t                m_commandBytesRead;
    u8*                    m_result;
    size_t                 m_resultSize;
};
