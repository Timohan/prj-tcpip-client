/*!
 * \file
 * \brief file prj_tcpip_client_lib_private.h
 *
 * library's private functionality
 *
 * Copyright of Timo Hannukkala. All rights reserved.
 *
 * \author Timo Hannukkala <timohannukkala@hotmail.com>
 */

#ifndef PRJ_TCPIP_CLIENT_LIB_PRIVATE_H
#define PRJ_TCPIP_CLIENT_LIB_PRIVATE_H

#include <cstdint>
#include <string>
#include <mutex>
#include <functional>
#include <atomic>
#include <thread>
#include <sys/types.h>
#include <netinet/in.h>
#include "../prj_tcpip_client_enum.h"
/*!
 * \brief PrjTcpipClientLibPrivate
 *
 */
class PrjTcpipClientLibPrivate
{
public:
    using Callback = std::function<void(TcpIpClientStatus status, const char *data, const ssize_t dataSize)>;

    explicit PrjTcpipClientLibPrivate();
    ~PrjTcpipClientLibPrivate();
    PrjTcpipClientLibPrivate(const PrjTcpipClientLibPrivate&) = delete;
    PrjTcpipClientLibPrivate& operator=(const PrjTcpipClientLibPrivate&) = delete;

    bool init(Callback callback);
    bool connectToServer(const std::string& ipAddress, uint16_t port);
    void disconnect();
    bool getIsConnected() const;
    bool sendText(const std::string& text, bool addLineBreakIfMissing);

private:
    // serializes init(), connectToServer(), disconnect() and the destructor
    std::mutex m_connectionMutex;
    // held while sending, so that texts sent from different threads don't mix;
    // m_socketFd is changed only while both m_connectionMutex and m_sendMutex are locked,
    // and only when the receive thread isn't running
    std::mutex m_sendMutex;
    int m_socketFd = -1;
    std::thread m_receiveThread;
    // true from a successful connect until the receive thread notices the disconnect
    std::atomic<bool> m_connected{false};
    // changed only when the receive thread isn't running
    Callback m_callback;

    void closeConnection();
    bool runCallback(TcpIpClientStatus status, const char *data, size_t dataSize);
    bool processLines(char *receivedData, size_t receivedSize, std::string& incompleteLine);
    void receiveLoop(int socketFd);
    static bool connectWithTimeout(int socketFd, const sockaddr_in& serverAddr);
    static bool sendAll(int socketFd, const std::string& text);
};

#endif // PRJ_TCPIP_CLIENT_LIB_PRIVATE_H
