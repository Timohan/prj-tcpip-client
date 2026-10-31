/*!
 * \file
 * \brief file prj_tcpip_client_lib.h
 *
 * public functionality of the TCP/IP client library
 *
 * Copyright of Timo Hannukkala. All rights reserved.
 *
 * \author Timo Hannukkala <timohannukkala@hotmail.com>
 */

#ifndef PRJ_TCPIP_CLIENT_LIB_H
#define PRJ_TCPIP_CLIENT_LIB_H

#include <cstdint>
#include <string>
#include <functional>
#include <sys/types.h>
#include "prj_tcpip_client_enum.h"

class PrjTcpipClientLibPrivate;

#ifndef PRJ_TCPIP_CLIENT_LIBRARY_EXPORT
#define PRJ_TCPIP_CLIENT_LIBRARY_EXPORT
#endif

/**
 * @brief Line-based TCP/IP client with one receive thread per connection
 *
 * The callback is always called from the receive thread, so it must be thread-safe.
 * Every Connected status is followed by exactly one Disconnected status.
 */
class PRJ_TCPIP_CLIENT_LIBRARY_EXPORT PrjTcpipClientLib
{
public:
    explicit PrjTcpipClientLib();
    /**
     * Disconnects and waits until the receive thread, including the Disconnected
     * callback, has finished. Must not be called from the callback; doing so aborts.
     */
    ~PrjTcpipClientLib();
    PrjTcpipClientLib(const PrjTcpipClientLib&) = delete;
    PrjTcpipClientLib& operator=(const PrjTcpipClientLib&) = delete;

    /**
     * Sets the callback. Returns false if callback is empty, if the client is
     * connected, or if called from the callback.
     *
     * callback gets Connected when a connection has been made (possibly after
     * connect() has returned), ReceivedData once per received line ('\n'-terminated,
     * with the newline and a trailing '\r' removed), and Disconnected when the
     * connection has ended. dataSize is 0 for Connected and Disconnected.
     * data is NUL-terminated, but the line itself may contain NUL bytes, so use
     * dataSize for its length. data is valid only until callback returns; copy it
     * to keep it. Data after the last newline is dropped on disconnect.
     * A line longer than 64 KiB, or an exception thrown from callback, disconnects.
     */
    bool init(std::function<void(TcpIpClientStatus status, const char *data, const ssize_t dataSize)> callback);
    /**
     * Connects to ipAddress (IPv4) and port, waiting at most 10 seconds. Returns false
     * if init() hasn't been called, if already connected, if called from the callback,
     * or if connecting fails.
     */
    bool connect(const std::string& ipAddress, uint16_t port);
    /**
     * Disconnects and waits until the Disconnected callback has returned. When called
     * from the callback, returns at once, and Disconnected follows after the callback
     * returns.
     */
    void disconnect();
    bool getIsConnected() const;
    /**
     * Sends text to the server. If addLineBreakIfMissing is set, '\n' is appended
     * when text doesn't already end with one, so empty text is sent as an empty line.
     * Returns false if text is empty and addLineBreakIfMissing isn't set, if not
     * connected, or if sending fails or takes over 10 seconds. In the last two cases
     * the client is disconnected, because the text may have been only partially sent.
     */
    bool sendText(const std::string& text, bool addLineBreakIfMissing);
private:
    PrjTcpipClientLibPrivate *m_private;
};

#endif // PRJ_TCPIP_CLIENT_LIB_H
