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
#include "CKLBDbgCommunicator.h"
#include "CKLBDebugTask.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
inline void writeNetworkU32(u8* destination, u32 value)
{
    destination[0] = value >> 24;
    destination[1] = value >> 16;
    destination[2] = value >> 8;
    destination[3] = value;
}

inline u32 readNetworkU32(const u8* source)
{
    u32 value = source[0];
    value = (value << 8) | source[1];
    value = (value << 8) | source[2];
    value = (value << 8) | source[3];
    return value;
}
}

CKLBDbgCommunicator::CKLBDbgCommunicator()
: m_connectionState(STATE_CLOSED)
, m_serverSocket(-1)
, m_clientSocket(-1)
{
}

CKLBDbgCommunicator::~CKLBDbgCommunicator()
{
    close(m_serverSocket);
    close(m_clientSocket);
}

void
CKLBDbgCommunicator::assignDebugger(CKLBDebuggerContext* debugger)
{
    m_debugger = debugger;
}

u8*
CKLBDbgCommunicator::allocateResult(u32 commandID, u32 size)
{
    m_resultSize = size + 8;
    m_result = new u8[m_resultSize];

    m_result[0] = commandID >> 24;
    m_result[1] = commandID >> 16;
    m_result[2] = commandID >> 8;
    m_result[3] = commandID;
    m_result[4] = size >> 24;
    m_result[5] = size >> 16;
    m_result[6] = size >> 8;
    m_result[7] = size;
    return m_result + 8;
}

void
CKLBDbgCommunicator::sendResult(u32 commandID, CKLBDebuggerContext::ERROR_TYPE error)
{
    writeNetworkU32(m_result, commandID);
    writeNetworkU32(m_result + 4, m_resultSize - 8);

    if (error) {
        writeNetworkU32(m_result, 0xcafebabe);
        writeNetworkU32(m_result + 4, 0);
        m_resultSize = 8;
    }

    size_t sent = 0;
    do {
        ssize_t written = write(m_clientSocket, m_result + sent, m_resultSize - sent);
        sent += written < 0 ? 0 : written;
    } while (sent < m_resultSize);
    delete [] m_result;
}

bool
CKLBDbgCommunicator::setupSocket()
{
    if (m_serverSocket > 0) {
        close(m_serverSocket);
        m_serverSocket = 0;
    }
    if (m_clientSocket > 0) {
        close(m_clientSocket);
        m_clientSocket = 0;
    }

    m_peerAddressSize = sizeof(m_peerAddress);
    sockaddr_in address = {};
    address.sin_port = htons(6543);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    bind(m_serverSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address));

    int flags = fcntl(m_serverSocket, F_GETFL, 0);
    fcntl(m_serverSocket, F_SETFL, flags | O_NONBLOCK);
    listen(m_serverSocket, 1);
    return true;
}

bool
CKLBDbgCommunicator::acceptClient()
{
    m_clientSocket = accept(
        m_serverSocket,
        reinterpret_cast<sockaddr*>(&m_peerAddress),
        &m_peerAddressSize);
    if (m_clientSocket < 0) {
        return false;
    }

    close(m_serverSocket);
    int flags = fcntl(m_serverSocket, F_GETFL, 0);
    fcntl(m_serverSocket, F_SETFL, flags | O_NONBLOCK);
    return true;
}

bool
CKLBDbgCommunicator::receiveCommand()
{
    ssize_t received;
    while (true) {
        if (m_receiveState == RECEIVE_HEADER) {
            received = read(
                m_clientSocket,
                m_header + m_headerBytesRead,
                sizeof(m_header) - m_headerBytesRead);
            if (received == 0) {
                if (errno == EAGAIN) {
                    return true;
                }
                close(m_clientSocket);
                m_connectionState = STATE_CLOSED;
                return false;
            }
            if (received < 0) {
                close(m_clientSocket);
                m_connectionState = STATE_CLOSED;
                return false;
            }

            m_headerBytesRead += received;
            if (m_headerBytesRead < static_cast<ssize_t>(sizeof(m_header))) {
                return true;
            }

            m_commandID = readNetworkU32(m_header);
            m_commandType = m_header[4];
            m_commandSize = readNetworkU32(m_header + 5);
            m_commandData = NULL;
            m_commandBytesRead = 0;
            if (m_commandSize > 0) {
                m_commandData = new u8[m_commandSize];
            }
            m_receiveState = RECEIVE_BODY;
        } else if (m_receiveState != RECEIVE_BODY) {
            continue;
        }

        if (m_commandSize > 0) {
            received = read(
                m_clientSocket,
                m_commandData + m_commandBytesRead,
                m_commandSize - m_commandBytesRead);
            if (received == 0) {
                if (errno != EAGAIN) {
                    close(m_clientSocket);
                    m_connectionState = STATE_CLOSED;
                    return true;
                }
            } else if (received < 0) {
                close(m_clientSocket);
                m_connectionState = STATE_CLOSED;
                return true;
            }
            m_commandBytesRead += received;
            if (m_commandBytesRead < m_commandSize) {
                return true;
            }
        }

        m_debugger->receiveCommand(
            m_commandID,
            static_cast<CKLBDebuggerContext::COMMAND_TYPE>(m_commandType),
            m_commandData,
            m_commandBytesRead);
        delete [] m_commandData;
        m_headerBytesRead = 0;
        m_receiveState = RECEIVE_HEADER;
    }
}

bool
CKLBDbgCommunicator::process()
{
    switch (m_connectionState) {
    case STATE_CLOSED:
        setupSocket();
        m_connectionState = STATE_LISTENING;
        m_headerBytesRead = 0;
    case STATE_LISTENING:
        if (!acceptClient()) {
            return false;
        }
        m_connectionState = STATE_CONNECTED;
        m_headerBytesRead = 0;
        m_receiveState = RECEIVE_HEADER;
    case STATE_CONNECTED:
        return receiveCommand();
    default:
        return true;
    }
}

CKLBDbgCommunicator&
CKLBDbgCommunicator::getInstance()
{
    static CKLBDbgCommunicator instance;
    return instance;
}

CKLBDebugTask::CKLBDebugTask()
{
}

CKLBDebugTask::~CKLBDebugTask()
{
}

CKLBDebugTask*
CKLBDebugTask::create(CKLBTask* /*parentTask*/, TASK_PHASE /*phase*/, CKLBDebuggerContext* debugger)
{
    CKLBDebugTask* task = KLBNEW(CKLBDebugTask);
    if (task) {
        debugger->setupReceiver(&CKLBDbgCommunicator::getInstance());
    }
    return task;
}

bool
CKLBDebugTask::initScript(CLuaState& lua)
{
    return CKLBDbgCommunicator::getInstance().process();
}

void
CKLBDebugTask::execute(u32 deltaT)
{
}
