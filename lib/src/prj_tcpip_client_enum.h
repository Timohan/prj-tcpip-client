/*!
 * \file
 * \brief file prj_tcpip_client_enum.h
 *
 * tcpip client enum library's public functionality
 *
 * Copyright of Timo Hannukkala. All rights reserved.
 *
 * \author Timo Hannukkala <timohannukkala@hotmail.com>
 */

#ifndef PRJ_TCPIP_CLIENT_ENUM_H
#define PRJ_TCPIP_CLIENT_ENUM_H

enum class TcpIpClientStatus {
    Connected = 0,
    Disconnected,
    ReceivedData
};

#endif // PRJ_TCPIP_CLIENT_ENUM_H
