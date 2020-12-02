#ifndef K15_HTML_SERVER_INCLUDE
#define K15_HTML_SERVER_INCLUDE

#include "k15_std/include/k15_container.hpp"
#include "k15_std/include/k15_memory.hpp"
#include "k15_std/include/k15_bitmask.hpp"
#include "k15_std/include/k15_path.hpp"
#include "k15_std/include/k15_io.hpp"

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
        file_handle       logFileHandle;
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

    enum class http_status_code
    {
        ok,
        not_found,
        bad_request
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
        const char*       pLogFilePath;
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

        file_handle logFileHandle = file_handle::invalid;
        if ( parameters.pLogFilePath != nullptr )
        {
            const result< file_handle > openLogFileResult = openFile( parameters.pLogFilePath, file_access::write, file_open_flag::clear_existing );
            if ( openLogFileResult.isOk() )
            {
                logFileHandle = openLogFileResult.getValue();
            }
        }

        memory_allocator* pAllocator = parameters.pAllocator;

        html_server* pServer   = newObject< html_server >( pAllocator );
        pServer->ipv4Socket    = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
        pServer->ipv6Socket    = socket( AF_INET6, SOCK_STREAM, IPPROTO_TCP );
        pServer->rootDirectory = parameters.pRootDirectory;
        pServer->port          = parameters.port;
        pServer->pAllocator    = pAllocator;
        pServer->logFileHandle = logFileHandle;

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

    result< void > findIndexFileInDirectory( path* pTarget, memory_allocator* pAllocator, const string_view& servePath )
    {
        K15_ASSERT( pTarget != nullptr );

        const static string_view indexFiles[] = {
            "index.html",
            "index.htm" };

        path indexFilePath( pAllocator );

        for ( size_t indexFileIndex = 0u; indexFileIndex < K15_ARRAY_SIZE( indexFiles ); ++indexFileIndex )
        {
            if ( !indexFilePath.setCombinedPath( servePath, indexFiles[ indexFileIndex ] ) )
            {
                return error_id::out_of_memory;
            }

            if ( doesFileExist( indexFilePath ) )
            {
                *pTarget = indexFilePath;
                return error_id::success;
            }

            indexFilePath.clear();
        }

        return error_id::not_found;
    }

    result< void > sendToClient( html_client* pClient, const array_view< char >& content )
    {
        WSASetLastError( 0u );
        const int bytesSend = send( pClient->socket, ( const char* )content.getStart(), content.getSize(), 0u );
        if ( bytesSend != SOCKET_ERROR )
        {
            return error_id::success;
        }

        const int wsaError = WSAGetLastError();
        K15_UNUSED_VARIABLE( wsaError );
        K15_NOT_IMPLEMENTED;

        return error_id::generic;
    }

    result< void > sendToClient( html_client* pClient, char character )
    {
        WSASetLastError( 0u );
        const int bytesSend = send( pClient->socket, &character, 1u, 0u );
        if ( bytesSend != SOCKET_ERROR )
        {
            return error_id::success;
        }

        const int wsaError = WSAGetLastError();
        K15_UNUSED_VARIABLE( wsaError );
        K15_NOT_IMPLEMENTED;

        return error_id::generic;
    }

    result< void > sendStatusCodeToClient( html_client* pClient, http_status_code statusCode )
    {
        switch ( statusCode )
        {
        case http_status_code::ok:
            {
                const char message[] = {
                    "HTTP/1.1 200 OK\n"
                    "Content-Type: text/html\n"
                    "Connection: close\n\n" };

                return sendToClient( pClient, createArrayView( message ) );
            }
        case http_status_code::not_found:
            {
                const char message[] = {
                    "HTTP/1.1 404 Not Found\n" };

                return sendToClient( pClient, createArrayView( message ) );
            }
        case http_status_code::bad_request:
            {
                const char message[] = {
                    "HTTP/1.1 400 Bad Request\n" };

                return sendToClient( pClient, createArrayView( message ) );
            }
        }
        return error_id::not_found;
    }

    result< void > sendFileContentToClient( html_client* pClient, const string_view& filePath )
    {
        file_handle_scope fileScope( filePath, file_access_mask( file_access::read ) );
        if ( fileScope.hasError() )
        {
            return fileScope.getError();
        }

        dynamic_array< char > fileContentBuffer;
        if ( !fileContentBuffer.create( pClient->pAllocator, K15_MiB( 1 ) ) )
        {
            return error_id::out_of_memory;
        }

        const file_handle requestedFileHandle = fileScope.getHandle();
        size_t            fileOffsetInBytes   = 0u;
        while ( true )
        {
            const result< size_t > readResult = readFromFile( requestedFileHandle, fileOffsetInBytes, &fileContentBuffer, fileContentBuffer.getCapacity() );
            if ( readResult.hasError() )
            {
                return readResult.getError();
            }

            const size_t bytesRead = readResult.getValue();
            fileOffsetInBytes += bytesRead;

            const result< void > sendResult = sendToClient( pClient, fileContentBuffer );
            if ( sendResult.hasError() )
            {
                return sendResult;
            }

            if ( bytesRead != fileContentBuffer.getCapacity() )
            {
                break;
            }
        }

        return sendToClient( pClient, "\0\n" );
    }

    void closeClientConnection( html_server* pServer, html_client* pClient )
    {
        closesocket( pClient->socket );
        deleteObject( pClient, pServer->pAllocator );
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
            if ( requestResult.hasError() )
            {
                sendStatusCodeToClient( pClient, http_status_code::bad_request );
                closeClientConnection( pServer, pClient );
                continue;
            }

            const html_request& request = requestResult.getValue();
            switch ( request.method )
            {
            case request_method::get:
                {
                    path servePath( pServer->pAllocator );
                    servePath.setCombinedPath( pServer->rootDirectory, request.path );

                    if ( servePath.isDirectory() )
                    {
                        const result< void > indexFilePathResult = findIndexFileInDirectory( &servePath, pServer->pAllocator, servePath );
                        if ( indexFilePathResult.hasError() )
                        {
                            return false;
                        }
                    }

                    if ( !doesFileExist( servePath ) )
                    {
                        sendStatusCodeToClient( pClient, http_status_code::not_found );
                    }
                    else
                    {
                        const result< void > statusCodeResult = sendStatusCodeToClient( pClient, http_status_code::ok );
                        if ( statusCodeResult.isOk() )
                        {
                            sendFileContentToClient( pClient, servePath );
                        }
                    }

                    closeClientConnection( pServer, pClient );
                    break;
                }
            }
        }
    }
} // namespace k15

#endif //K15_HTML_SERVER_INCLUDE