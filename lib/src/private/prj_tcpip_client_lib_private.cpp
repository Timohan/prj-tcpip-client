#include "prj_tcpip_client_lib_private.h"
#include "../tg_global_log.h"
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <exception>
#include <system_error>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

const int BUFFER_SIZE = 16 * 1024;
const size_t MAX_LINE_LENGTH = 64 * 1024;
const int CONNECT_TIMEOUT_SECONDS = 10;
const int SEND_TIMEOUT_SECONDS = 10;

// set in the receive thread; used to detect calls from the client's own callback
static thread_local const PrjTcpipClientLibPrivate *t_receiveThreadClient = nullptr;

/**
 * returns the milliseconds left until deadline, or 0 if it has passed
 */
static int remainingMs(std::chrono::steady_clock::time_point deadline)
{
    TG_FUNCTION_BEGIN();
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
    TG_FUNCTION_END();
    return remaining > 0 ? static_cast<int>(remaining) : 0;
}

/**
 * returns true if a line break must be appended to text before sending it
 */
static bool needsLineBreak(const std::string& text, bool addLineBreakIfMissing)
{
    TG_FUNCTION_BEGIN();
    TG_FUNCTION_END();
    return addLineBreakIfMissing && (text.empty() || text.back() != '\n');
}

PrjTcpipClientLibPrivate::PrjTcpipClientLibPrivate()
{
    TG_FUNCTION_BEGIN();
    TG_FUNCTION_END();
}

/**
 * disconnects and waits for the receive thread, so that it doesn't use this object
 * after it has been destroyed
 */
PrjTcpipClientLibPrivate::~PrjTcpipClientLibPrivate()
{
    TG_FUNCTION_BEGIN();
    if (t_receiveThreadClient == this) {
        // waiting for our own thread would deadlock
        TG_ERROR_LOG("Client destroyed from its own callback");
        std::abort();
    }
    std::lock_guard<std::mutex> lock(m_connectionMutex);
    closeConnection();
    TG_FUNCTION_END();
}

bool PrjTcpipClientLibPrivate::init(Callback callback)
{
    TG_FUNCTION_BEGIN();
    if (t_receiveThreadClient == this) {
        TG_ERROR_LOG("init() can't be called from the callback");
        TG_FUNCTION_END();
        return false;
    }
    if (!callback) {
        TG_ERROR_LOG("Callback is empty");
        TG_FUNCTION_END();
        return false;
    }
    std::lock_guard<std::mutex> lock(m_connectionMutex);
    if (m_connected) {
        TG_ERROR_LOG("init() can't be called while connected");
        TG_FUNCTION_END();
        return false;
    }
    // a receive thread that has already noticed the disconnect may still be calling the old callback
    closeConnection();
    m_callback = std::move(callback);
    TG_FUNCTION_END();
    return true;
}

/**
 * connects with a non-blocking connect, so that an unreachable server can't block for minutes
 * @return true if connected within CONNECT_TIMEOUT_SECONDS; the socket is left blocking
 */
bool PrjTcpipClientLibPrivate::connectWithTimeout(int socketFd, const sockaddr_in& serverAddr)
{
    TG_FUNCTION_BEGIN();
    const int flags = fcntl(socketFd, F_GETFL, 0);
    if (flags < 0 || fcntl(socketFd, F_SETFL, flags | O_NONBLOCK) < 0) {
        TG_ERROR_LOG("Failed to make the socket non-blocking", std::strerror(errno));
        TG_FUNCTION_END();
        return false;
    }
    // EINTR doesn't stop a non-blocking connect, it continues like EINPROGRESS
    if (::connect(socketFd, reinterpret_cast<const sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0
            && errno != EINPROGRESS && errno != EINTR) {
        TG_ERROR_LOG("Connection failed", std::strerror(errno));
        TG_FUNCTION_END();
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(CONNECT_TIMEOUT_SECONDS);
    while (true) {
        pollfd pfd{socketFd, POLLOUT, 0};
        const int ret = poll(&pfd, 1, remainingMs(deadline));
        if (ret > 0) {
            break;
        }
        if (ret == 0) {
            TG_ERROR_LOG("Connection timed out");
            TG_FUNCTION_END();
            return false;
        }
        if (errno != EINTR) {
            TG_ERROR_LOG("Waiting for the connection failed", std::strerror(errno));
            TG_FUNCTION_END();
            return false;
        }
    }

    int error = 0;
    socklen_t errorLen = sizeof(error);
    if (getsockopt(socketFd, SOL_SOCKET, SO_ERROR, &error, &errorLen) < 0) {
        error = errno;
    }
    if (error != 0) {
        TG_ERROR_LOG("Connection failed", std::strerror(error));
        TG_FUNCTION_END();
        return false;
    }
    if (fcntl(socketFd, F_SETFL, flags) < 0) {
        TG_ERROR_LOG("Failed to make the socket blocking", std::strerror(errno));
        TG_FUNCTION_END();
        return false;
    }
    TG_FUNCTION_END();
    return true;
}

bool PrjTcpipClientLibPrivate::connectToServer(const std::string& ipAddress, uint16_t port)
{
    TG_FUNCTION_BEGIN();
    if (t_receiveThreadClient == this) {
        // the old receive thread is the current thread, so it can't be waited for
        TG_ERROR_LOG("connect() can't be called from the callback");
        TG_FUNCTION_END();
        return false;
    }
    std::lock_guard<std::mutex> lock(m_connectionMutex);
    if (!m_callback) {
        TG_ERROR_LOG("init() must be called before connect()");
        TG_FUNCTION_END();
        return false;
    }
    if (m_connected) {
        TG_INFO_LOG("Could not start connect. Client is already connected.");
        TG_FUNCTION_END();
        return false;
    }
    // the previous connection has ended, but its thread and socket may not have been cleaned up yet
    closeConnection();

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, ipAddress.c_str(), &serverAddr.sin_addr) != 1) {
        TG_ERROR_LOG("Invalid IP address", ipAddress);
        TG_FUNCTION_END();
        return false;
    }

    const int socketFd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socketFd < 0) {
        TG_ERROR_LOG("Socket creation failed", std::strerror(errno));
        TG_FUNCTION_END();
        return false;
    }

    TG_INFO_LOG("Connecting to", ipAddress, port);
    if (!connectWithTimeout(socketFd, serverAddr)) {
        close(socketFd);
        TG_FUNCTION_END();
        return false;
    }

    m_sendMutex.lock();
    m_socketFd = socketFd;
    m_sendMutex.unlock();
    m_connected = true;
    try {
        m_receiveThread = std::thread(&PrjTcpipClientLibPrivate::receiveLoop, this, socketFd);
    } catch (const std::system_error& e) {
        TG_ERROR_LOG("Failed to create the receive thread", e.what());
        (void)e;
        m_connected = false;
        closeConnection();
        TG_FUNCTION_END();
        return false;
    }
    TG_INFO_LOG("Connected to", ipAddress, port);
    TG_FUNCTION_END();
    return true;
}

/**
 * shuts the socket down, waits for the receive thread and closes the socket;
 * m_connectionMutex must be locked
 */
void PrjTcpipClientLibPrivate::closeConnection()
{
    TG_FUNCTION_BEGIN();
    if (m_socketFd >= 0) {
        // wakes up the receive thread and blocked sends; the socket is closed only after
        // the thread has finished, so that its number can't be reused while still in use
        shutdown(m_socketFd, SHUT_RDWR);
    }
    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }
    std::lock_guard<std::mutex> sendLock(m_sendMutex);
    if (m_socketFd >= 0) {
        TG_INFO_LOG("Disconnected from the server");
        close(m_socketFd);
        m_socketFd = -1;
    }
    TG_FUNCTION_END();
}

void PrjTcpipClientLibPrivate::disconnect()
{
    TG_FUNCTION_BEGIN();
    if (t_receiveThreadClient == this) {
        // the receive thread can't wait for itself; it ends after the callback returns.
        // m_socketFd can't change while the receive thread is running
        shutdown(m_socketFd, SHUT_RDWR);
        TG_FUNCTION_END();
        return;
    }
    std::lock_guard<std::mutex> lock(m_connectionMutex);
    closeConnection();
    TG_FUNCTION_END();
}

bool PrjTcpipClientLibPrivate::getIsConnected() const
{
    TG_FUNCTION_BEGIN();
    TG_FUNCTION_END();
    return m_connected;
}

/**
 * sends the whole text, because send() may write only part of it in one call
 * @return true if all bytes were sent within SEND_TIMEOUT_SECONDS
 */
bool PrjTcpipClientLibPrivate::sendAll(int socketFd, const std::string& text)
{
    TG_FUNCTION_BEGIN();
    // one deadline for the whole text, so that a slowly reading server can't block senders forever
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(SEND_TIMEOUT_SECONDS);
    size_t totalSent = 0;
    while (totalSent < text.size()) {
        // MSG_NOSIGNAL: a closed connection must not kill the process with SIGPIPE
        ssize_t bytesSent = send(socketFd, text.data() + totalSent, text.size() - totalSent,
                                 MSG_NOSIGNAL | MSG_DONTWAIT);
        if (bytesSent >= 0) {
            totalSent += static_cast<size_t>(bytesSent);
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            TG_FUNCTION_END();
            return false;
        }
        const int remaining = remainingMs(deadline);
        if (remaining <= 0) {
            TG_FUNCTION_END();
            return false;
        }
        pollfd pfd{socketFd, POLLOUT, 0};
        if (poll(&pfd, 1, remaining) < 0 && errno != EINTR) {
            TG_FUNCTION_END();
            return false;
        }
    }
    TG_FUNCTION_END();
    return true;
}

/**
 * sends text to the server
 * @param text text to send
 * @param addLineBreakIfMissing append '\n' if text doesn't end with one
 */
bool PrjTcpipClientLibPrivate::sendText(const std::string& text, bool addLineBreakIfMissing)
{
    TG_FUNCTION_BEGIN();
    // empty text is only sent as an empty line
    if (text.empty() && !addLineBreakIfMissing) {
        TG_FUNCTION_END();
        return false;
    }
    std::lock_guard<std::mutex> sendLock(m_sendMutex);
    if (!m_connected || m_socketFd < 0) {
        TG_ERROR_LOG("Cannot send, not connected");
        TG_FUNCTION_END();
        return false;
    }
    const bool sent = needsLineBreak(text, addLineBreakIfMissing)
            ? sendAll(m_socketFd, text + "\n") : sendAll(m_socketFd, text);
    if (!sent) {
        TG_ERROR_LOG("Failed to send data, disconnecting", std::strerror(errno));
        // the receive thread notices this and reports Disconnected
        shutdown(m_socketFd, SHUT_RDWR);
    }
    TG_FUNCTION_END();
    return sent;
}

/**
 * runs the user callback; exceptions are logged instead of terminating the process
 * @return false if the callback threw
 */
bool PrjTcpipClientLibPrivate::runCallback(TcpIpClientStatus status, const char *data, size_t dataSize)
{
    TG_FUNCTION_BEGIN();
    try {
        m_callback(status, data, static_cast<ssize_t>(dataSize));
        TG_FUNCTION_END();
        return true;
    } catch (const std::exception& e) {
        TG_ERROR_LOG("Callback threw an exception", static_cast<int>(status), e.what());
        (void)e;
    } catch (...) {
        TG_ERROR_LOG("Callback threw an unknown exception", static_cast<int>(status));
    }
    TG_FUNCTION_END();
    return false;
}

/**
 * splits the complete lines from the received data and passes each one to the callback;
 * a line that is whole in receivedData is passed directly from it without copying, and only
 * a line that continues in the next read is stored in incompleteLine, so each byte is searched
 * only once and copied at most once
 * @param receivedData data from the latest read; each line's '\r' or '\n' is overwritten
 * with '\0', so that the callback gets a NUL-terminated line
 * @param receivedSize size of receivedData
 * @param incompleteLine start of a line received in earlier reads, updated with the new incomplete line
 * @return false if the callback threw, in which case the client should disconnect
 */
bool PrjTcpipClientLibPrivate::processLines(char *receivedData, size_t receivedSize, std::string& incompleteLine)
{
    TG_FUNCTION_BEGIN();
    char *receivedEnd = receivedData + receivedSize;
    char *lineStart = receivedData;
    char *lineEnd;
    while ((lineEnd = static_cast<char*>(std::memchr(lineStart, '\n',
                static_cast<size_t>(receivedEnd - lineStart)))) != nullptr) {
        const char *data;
        size_t dataSize;
        if (incompleteLine.empty()) {
            dataSize = static_cast<size_t>(lineEnd - lineStart);
            if (dataSize > 0 && lineStart[dataSize - 1] == '\r') {
                dataSize--;
            }
            lineStart[dataSize] = '\0';
            data = lineStart;
        } else {
            // the line started in an earlier read
            incompleteLine.append(lineStart, lineEnd);
            if (incompleteLine.back() == '\r') {
                incompleteLine.pop_back();
            }
            data = incompleteLine.c_str();
            dataSize = incompleteLine.size();
        }
        TG_DEBUG_LOG("Read from server", std::string_view(data, dataSize));
        const bool callbackOk = runCallback(TcpIpClientStatus::ReceivedData, data, dataSize);
        // cleared only after the callback, because data may point to it
        incompleteLine.clear();
        if (!callbackOk) {
            TG_FUNCTION_END();
            return false;
        }
        lineStart = lineEnd + 1;
    }
    incompleteLine.append(lineStart, receivedEnd);
    TG_FUNCTION_END();
    return true;
}

/**
 * reads from the server until the connection ends; the thread is always joined by
 * closeConnection(), never by itself
 * @param socketFd the socket, which stays open until this thread has been joined
 */
void PrjTcpipClientLibPrivate::receiveLoop(int socketFd)
{
    TG_FUNCTION_BEGIN();
    t_receiveThreadClient = this;
    bool connected = runCallback(TcpIpClientStatus::Connected, "", 0);
    char buffer[BUFFER_SIZE];
    std::string incompleteLine;
    while (connected) {
        ssize_t bytesRead = recv(socketFd, buffer, BUFFER_SIZE, 0);
        if (bytesRead < 0) {
            if (errno == EINTR) {
                continue;
            }
            TG_WARNING_LOG("Failed to read from server", std::strerror(errno));
            break;
        } else if (bytesRead == 0) {
            TG_INFO_LOG("Connection closed");
            break;
        }

        // TCP is a byte stream, so collect the data and pass complete lines to the callback
        connected = processLines(buffer, static_cast<size_t>(bytesRead), incompleteLine);

        if (incompleteLine.size() > MAX_LINE_LENGTH) {
            TG_ERROR_LOG("Line too long, disconnecting");
            break;
        }
    }
    // stops sends to a connection that has ended
    shutdown(socketFd, SHUT_RDWR);
    m_connected = false;
    runCallback(TcpIpClientStatus::Disconnected, "", 0);
    t_receiveThreadClient = nullptr;
    TG_FUNCTION_END();
}
