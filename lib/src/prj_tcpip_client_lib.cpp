#include "prj_tcpip_client_lib.h"
#include "private/prj_tcpip_client_lib_private.h"
#include "tg_global_log.h"


PrjTcpipClientLib::PrjTcpipClientLib()
    : m_private(new PrjTcpipClientLibPrivate())
{
    TG_FUNCTION_BEGIN();
    TG_FUNCTION_END();
}

PrjTcpipClientLib::~PrjTcpipClientLib()
{
    TG_FUNCTION_BEGIN();
    delete m_private;
    TG_FUNCTION_END();
}

bool PrjTcpipClientLib::init(std::function<void(TcpIpClientStatus status, const char *data, const ssize_t dataSize)> callback)
{
    TG_FUNCTION_BEGIN();
    const bool ret = m_private->init(std::move(callback));
    TG_FUNCTION_END();
    return ret;
}

bool PrjTcpipClientLib::connect(const std::string& ipAddress, uint16_t port)
{
    TG_FUNCTION_BEGIN();
    const bool ret = m_private->connectToServer(ipAddress, port);
    TG_FUNCTION_END();
    return ret;
}

void PrjTcpipClientLib::disconnect()
{
    TG_FUNCTION_BEGIN();
    m_private->disconnect();
    TG_FUNCTION_END();
}

bool PrjTcpipClientLib::getIsConnected() const
{
    TG_FUNCTION_BEGIN();
    const bool ret = m_private->getIsConnected();
    TG_FUNCTION_END();
    return ret;
}

bool PrjTcpipClientLib::sendText(const std::string& text, bool addLineBreakIfMissing)
{
    TG_FUNCTION_BEGIN();
    const bool ret = m_private->sendText(text, addLineBreakIfMissing);
    TG_FUNCTION_END();
    return ret;
}
