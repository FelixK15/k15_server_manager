#ifndef K15_HTML_SERVER_INCLUDE
#define K15_HTML_SERVER_INCLUDE

#include "k15_std/include/k15_container.hpp"
#include "k15_std/include/k15_memory.hpp"
#include "k15_std/include/k15_bitmask.hpp"
#include "k15_std/include/k15_path.hpp"

namespace k15
{
    typedef int socketId;

    enum class html_server_flag
    {
        only_serve_below_root = 0
    };

    using html_server_flags = bitmask8< html_server_flag >;

    struct html_server
    {
        memory_allocator* pAllocator;
        socketId          ipv4Socket;
        socketId          ipv6Socket;
        string_view       rootDirectory;
        int               port;

        html_server_flags flags;
    };

    struct html_client
    {
        memory_allocator* pAllocator;
        socketId          socket;
    };

    enum class request_method
    {
        get,
        post,
        put,
        del
    };

    enum : uint8
    {
        HtmlRequestPathLength = 128
    };

    struct html_request
    {
        request_method method;
        char           path[ HtmlRequestPathLength ];
    };

    struct html_server_parameters
    {
        memory_allocator* pAllocator;
        int               port;
        const char*       pIpv4BindAddress;
        const char*       pIpv6BindAddress;
        const char*       pRootDirectory;
        bool              onlyServeBelowRoot; //FK: Don't allow paths like ../file.txt
    };

    bool listenOnSocket( const socketId& socket, int protocol, int port, const char* bindAddress )
    {
        if ( socket == INVALID_SOCKET )
        {
            return false;
        }

        sockaddr_in sockAddr;
        sockAddr.sin_family      = protocol;
        sockAddr.sin_port        = htons( port );
        sockAddr.sin_addr.s_addr = inet_addr( bindAddress );

        const int bindResult = bind( socket, ( const struct sockaddr* )&sockAddr, sizeof( sockAddr ) );
        if ( bindResult == SOCKET_ERROR )
        {
            return false;
        }

        const int backlog      = 10; //FK: TODO: find reasonable number here
        const int listenResult = listen( socket, backlog );

        if ( listenResult == SOCKET_ERROR )
        {
            return false;
        }

        return true;
    }

    html_client* waitForClientConnection( html_server* pServer )
    {
        fd_set readSockets;
        FD_ZERO( &readSockets );
        FD_SET( pServer->ipv4Socket, &readSockets );
        FD_SET( pServer->ipv6Socket, &readSockets );

        const int selectResult = select( 0, &readSockets, nullptr, nullptr, nullptr );
        if ( selectResult == -1 )
        {
            return nullptr;
        }

        html_client* pClient = newObject< html_client >( pServer->pAllocator );
        pClient->pAllocator  = pServer->pAllocator;
        if ( FD_ISSET( pServer->ipv4Socket, &readSockets ) )
        {
            pClient->socket = accept( pServer->ipv4Socket, NULL, NULL );
        }
        else
        {
            pClient->socket = accept( pServer->ipv6Socket, NULL, NULL );
        }

        return pClient;
    }

    result< void > receiveClientData( slice< char >* pMessageBuffer, html_client* pClient )
    {
        while ( true )
        {
            char      buffer[ 256 ];
            const int bytesRead = recv( pClient->socket, buffer, 256, 0u );

            if ( bytesRead == -1 )
            {
                return error_id::socket_error;
            }
            else if ( bytesRead == 0 )
            {
                pMessageBuffer->pushBack( '\0' );
                return error_id::success;
            }

            char* pMessage = pMessageBuffer->pushBackRange( bytesRead );
            if ( pMessage == nullptr )
            {
                return error_id::out_of_memory;
            }

            copyMemoryNonOverlapping( pMessage, bytesRead, buffer, bytesRead );

            if ( bytesRead < sizeof( buffer ) )
            {
                pMessageBuffer->pushBack( '\0' );
                return error_id::success;
            }
        }

        return error_id::success;
    }

    result< html_request > parseHtmlRequest( slice< char >* pMessageBuffer )
    {
        html_request request;

        const char* pMessageRunningPtr = pMessageBuffer->getStart();
        const char* pMessageEnd        = pMessageBuffer->getEnd();

        enum class parse_state
        {
            method,
            path,
            finished
        };

        parse_state state = parse_state::method;

        while ( true )
        {
            if ( pMessageRunningPtr == pMessageEnd )
            {
                return error_id::parse_error;
            }

            if ( state == parse_state::finished )
            {
                break;
            }

            switch ( state )
            {
            case parse_state::method:
                {
                    if ( compareAsciiStringNonCaseSensitive( pMessageRunningPtr, "get" ) )
                    {
                        request.method = request_method::get;
                        state          = parse_state::path;
                    }
                    else if ( compareAsciiStringNonCaseSensitive( pMessageRunningPtr, "post" ) )
                    {
                        request.method = request_method::post;
                        state          = parse_state::path;
                    }
                    else if ( compareAsciiStringNonCaseSensitive( pMessageRunningPtr, "put" ) )
                    {
                        request.method = request_method::put;
                        state          = parse_state::path;
                    }
                    else if ( compareAsciiStringNonCaseSensitive( pMessageRunningPtr, "delete" ) )
                    {
                        request.method = request_method::del;
                        state          = parse_state::path;
                    }
                    else
                    {
                        return error_id::not_supported;
                    }

                    pMessageRunningPtr = findNextAsciiWhiteSpace( pMessageRunningPtr );
                    ++pMessageRunningPtr;

                    break;
                }

            case parse_state::path:
                {
                    const char* pPathBegin = pMessageRunningPtr;
                    const char* pPathEnd   = findNextAsciiWhiteSpace( pPathBegin );
                    if ( pPathEnd == pPathBegin )
                    {
                        return error_id::parse_error;
                    }

                    const size_t pathLength = pPathEnd - pPathBegin;
                    K15_ASSERT( pathLength < HtmlRequestPathLength );

                    copyMemoryNonOverlapping( request.path, HtmlRequestPathLength, pPathBegin, pathLength );
                    request.path[ pathLength ] = 0;

                    state = parse_state::finished;
                    break;
                }
            }
        }

        return request;
    }

    result< html_request > readClientRequest( html_client* pClient )
    {
        //html_request request;
        //dynamic_array< char, 256u > messageBuffer;
        dynamic_array< char > messageBuffer( pClient->pAllocator );
        const result< void >  receiveResult = receiveClientData( &messageBuffer, pClient );
        if ( receiveResult.hasError() )
        {
            return receiveResult.getError();
        }

        return parseHtmlRequest( &messageBuffer );
    }

    void destroyHtmlServer( html_server* pServer )
    {
        if ( pServer->ipv4Socket != INVALID_SOCKET )
        {
            closesocket( pServer->ipv4Socket );
            pServer->ipv4Socket = INVALID_SOCKET;
        }

        if ( pServer->ipv6Socket != INVALID_SOCKET )
        {
            closesocket( pServer->ipv6Socket );
            pServer->ipv6Socket = INVALID_SOCKET;
        }

        deleteObject( pServer, pServer->pAllocator );
    }

    result< html_server* > createHtmlServer( const html_server_parameters& parameters )
    {
        K15_ASSERT( parameters.pAllocator != nullptr );
        K15_ASSERT( parameters.pRootDirectory != nullptr );
        memory_allocator* pAllocator = parameters.pAllocator;

        html_server* pServer   = newObject< html_server >( pAllocator );
        pServer->ipv4Socket    = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
        pServer->ipv6Socket    = socket( AF_INET6, SOCK_STREAM, IPPROTO_TCP );
        pServer->rootDirectory = parameters.pRootDirectory;
        pServer->port          = parameters.port;
        pServer->pAllocator    = pAllocator;

        if ( pServer->ipv4Socket == INVALID_SOCKET && pServer->ipv6Socket == INVALID_SOCKET )
        {
            destroyHtmlServer( pServer );
            return error_id::socket_error;
        }

        if ( !listenOnSocket( pServer->ipv4Socket, AF_INET, parameters.port, parameters.pIpv4BindAddress ) && !listenOnSocket( pServer->ipv6Socket, AF_INET6, parameters.port, parameters.pIpv6BindAddress ) )
        {
            destroyHtmlServer( pServer );
            return error_id::listen_error;
        }

        pServer->flags.setIf( html_server_flag::only_serve_below_root, parameters.onlyServeBelowRoot );

        return pServer;
    }

    bool serveHtmlClients( html_server* pServer )
    {
        while ( true )
        {
            html_client* pClient = waitForClientConnection( pServer );
            if ( pClient == nullptr )
            {
                continue;
            }

            const result< html_request > requestResult = readClientRequest( pClient );
            const html_request&          request       = requestResult.getValue();

            switch ( request.method )
            {
            case request_method::get:
                {
                    path servePath( pServer->pAllocator );
                    servePath.setCombinedPath( pServer->rootDirectory, request.path );
#if 0
                    if ( servePath.isDirectory() )
                    {
                        const result< string_view > indexFilePathResult = findIndexFileInDirectory( servePath );
                        if ( indexFilePathResult.hasError() )
                        {
                            return false;
                        }

                        path.setAbsolutePath( indexFilePathResult.getValue() );
                    }

                    if ( !doesFileExist( servePath ) )
                    {
                        return false;
                    }

                    sendFileContentToClient( pClient, servePath );
#endif

                    break;
                }
            }
        }
    }
} // namespace k15

#endif //K15_HTML_SERVER_INCLUDE