#ifndef K15_HTML_SERVER_INCLUDE
#define K15_HTML_SERVER_INCLUDE

#include <malloc.h>
#include "k15_std/include/k15_container.hpp"
#include "k15_std/include/k15_memory.hpp"

namespace k15
{
    typedef int socketId;

	struct html_server
	{
		socketId 			ipv4Socket;
		socketId 			ipv6Socket; 
		int 				port;
	};

    struct html_client
    {
        socketId socket;
    };

    enum class request_method
    {
        Get,
        Post
    };

    struct html_request
    {
        request_method  method;
        const char*     pPath;
    };

	struct html_server_parameters
	{
		int port;
		const char* ipv4BindAddress;
		const char* ipv6BindAddress;
	};

    bool listenOnSocket(const socketId& socket, int protocol, int port, const char* bindAddress)
    {
        if (socket == INVALID_SOCKET)
        {
            return false;
        }

        sockaddr_in sockAddr;
        sockAddr.sin_family         = protocol;	
        sockAddr.sin_port           = htons(port);
        sockAddr.sin_addr.s_addr    = inet_addr(bindAddress);

        const int bindResult = bind( socket, (const struct sockaddr*)&sockAddr, sizeof(sockAddr) );
        if( bindResult == SOCKET_ERROR )
        {
            return false;
        }

        const int backlog = 10; //FK: TODO: find reasonable number here
        const int listenResult = listen(socket, backlog);

        if( listenResult == SOCKET_ERROR )
        {
            return false;
        }

        return true;
    }

    html_client* waitForClientConnection(html_server* pServer)
    {
        fd_set readSockets;
        FD_ZERO(&readSockets);
        FD_SET(pServer->ipv4Socket, &readSockets);
        FD_SET(pServer->ipv6Socket, &readSockets);

        const int selectResult = select(0, &readSockets, nullptr, nullptr, nullptr);
        if (selectResult == -1)
        {
            return nullptr;
        }

        html_client* pClient = (html_client*)malloc(sizeof(html_client));
        if (FD_ISSET(pServer->ipv4Socket, &readSockets))
        {
            pClient->socket = accept(pServer->ipv4Socket, NULL, NULL);
        }
        else
        {
            pClient->socket = accept(pServer->ipv6Socket, NULL, NULL);
        }

        return pClient;
    }

    bool isWhiteSpace(char rune)
    {
        return (rune == ' ' || rune == '/t' || rune == '/r' || rune == '/n');
    }

    void parseClientRequest(html_request* pRequest, const char* pMessageBuffer, int messageBufferLength)
    {
        //FK: Add state machine
        const char* pToken = nullptr;
        int startTokenCharIndex = -1;
        for( int charIndex = 0; charIndex < messageBufferLength; ++charIndex)
        {
            const char messageBufferChar = pMessageBuffer[ charIndex ];

            //FK: eat whitespace
            if( isWhiteSpace( messageBufferChar ) )
            {
                continue;
            }

            if (startTokenCharIndex == -1)
            {
              startTokenCharIndex = charIndex;
            }

            if( ( charIndex + 1) == messageBufferLength )
            {
                
            }
            else
            {
                const char nextMessageBufferChar = pMessageBuffer[ charIndex + 1 ];
                if( isWhiteSpace( nextMessageBufferChar ) )
                {

                }
            }
        }
    }

    result<void> receiveClientData( slice< char >* pMessageBuffer, html_client* pClient)
    {
        while( true )
        {
            char buffer[256];
            const int bytesRead = recv( pClient->socket, buffer, 256, 0u );

            if (bytesRead == -1)
            {
                return error_id::socket_error;
            }
            else if (bytesRead == 0)
            {
                pMessageBuffer->pushBack('\0');
                return error_id::success;
            }

            char* pMessage = pMessageBuffer->pushBackRange(bytesRead);
            if (pMessage == nullptr)
            {
                return error_id::out_of_memory;
            }

            copyMemoryNonOverlapping(pMessage, bytesRead, buffer, bytesRead);

            if (bytesRead < sizeof(buffer))
            {
                pMessageBuffer->pushBack('\0');
                return error_id::success;
            }
        }

        return error_id::success;

    }

    result<html_request> parseHtmlRequest(slice< char >* pMessageBuffer)
    {
        const char* pMessage = pMessageBuffer->getStart();
        const char* pMessageEnd = pMessageBuffer->getEnd();

        enum class parse_state
        {
            method,
            pathBegin,
            pathEnd,
            finished
        };

        html_request request;
        K15_UNUSED_VARIABLE(request);

        uint32 tokenStartIndex  = 0u;
        uint32 tokenLength      = 0u;
        parse_state state = parse_state::method;

        while( true )
        {
            if( state == parse_state::finished )
            {
                break;
            }

            if( pMessage == pMessageEnd )
            { 
                return error_id::parse_error;
            }
        }

        //FK: TODO return result
        return error_id::success;
    }

    result< html_request > readClientRequest(html_client* pClient)
    {
        //html_request request;
        //dynamic_array< char, 256u > messageBuffer;
        dynamic_array< char > messageBuffer;
        const result< void > receiveResult = receiveClientData(&messageBuffer, pClient);
        if (receiveResult.hasError())
        {
            return receiveResult.getError();
        }

       return parseHtmlRequest( &messageBuffer );
    }

    result<html_server*> createHtmlServer(const html_server_parameters& parameters)
    {
        //FK: TODO: use own allocation strategy
        html_server* pServer = (html_server*)malloc(sizeof(html_server));
        pServer->ipv4Socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	    pServer->ipv6Socket = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);

	    if (pServer->ipv4Socket == INVALID_SOCKET && pServer->ipv6Socket == INVALID_SOCKET)
	    {
	    	return error_id::socket_error;	
	    }

	    if (!listenOnSocket(pServer->ipv4Socket, AF_INET, parameters.port, parameters.ipv4BindAddress) && ! listenOnSocket(pServer->ipv4Socket, AF_INET6, parameters.port, parameters.ipv6BindAddress) )
        {
            return error_id::listen_error;
        }

        return pServer;
    }

    bool serveHtmlClients(html_server* pServer)
    {
        while( true )
        {
            html_client* pClient = waitForClientConnection(pServer);
            if (pClient == nullptr)
            {
                continue;
            }

            const result<html_request> requestResult = readClientRequest(pClient);
            const html_request& request = requestResult.getValue();
            K15_UNUSED_VARIABLE(request);
        }
    }
}

#endif //K15_HTML_SERVER_INCLUDE