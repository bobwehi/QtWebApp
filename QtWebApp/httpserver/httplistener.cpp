/**
  @file
  @author Stefan Frings
*/

#include "httplistener.h"
#include "httpconnectionhandler.h"
#include "httpconnectionhandlerpool.h"
#include <QCoreApplication>

using namespace stefanfrings;

HttpListener::HttpListener(QSettings* settings, HttpRequestHandler* requestHandler, QObject *parent)
    : QTcpServer(parent)
{
    Q_ASSERT(settings!=0);
    Q_ASSERT(requestHandler!=0);
    pool=NULL;
    this->settings=settings;
    this->requestHandler=requestHandler;
    // Reqister type of socketDescriptor for signal/slot handling
    qRegisterMetaType<tSocketDescriptor>("tSocketDescriptor");
    // Start listening
    listen();
}


HttpListener::~HttpListener()
{
    close();
    qDebug("HttpListener: destroyed");
}


void HttpListener::listen()
{
    run = true;
    if (!pool)
    {
        pool=new HttpConnectionHandlerPool(settings,requestHandler);
    }
    QString host = settings->value("host").toString();
    int port=settings->value("port").toInt();
    QTcpServer::listen(host.isEmpty() ? QHostAddress::Any : QHostAddress(host), port);
    if (!isListening())
    {
        qCritical("HttpListener: Cannot bind on port %i: %s",port,qPrintable(errorString()));
    }
    else {
        qDebug("HttpListener: Listening on port %i",port);
    }
}

void HttpListener::stop()
{
   run = false;
}

void HttpListener::close() {

    try
    {
        poolMutex.lockForWrite();
        QTcpServer::close();
        qDebug("HttpListener: closed");
        if (pool) {
            delete pool;
            pool=NULL;
        }
        poolMutex.unlock();
    }
    catch (...)
    {
        poolMutex.unlock();
        qCritical("HttpListener (%p): An uncatched exception occured in close method", this);
        throw;
    }
}

void HttpListener::incomingConnection(tSocketDescriptor socketDescriptor) {
#ifdef SUPERVERBOSE
   qDebug("HttpListener: New connection");
#endif

    bool serviceAvailable = false;
    if (run)
    {
       try
       {
          poolMutex.lockForRead();
          if (isListening())
          {
             serviceAvailable = true;
             HttpConnectionHandler* freeHandler=NULL;
             if (pool)
             {
                freeHandler=pool->getConnectionHandler();
             }

             // Let the handler process the new connection.
             if (freeHandler)
             {
                // The descriptor is passed via event queue because the handler lives in another thread
                QMetaObject::invokeMethod(freeHandler, "handleConnection", Qt::QueuedConnection, Q_ARG(tSocketDescriptor, socketDescriptor));
             }
             else
             {
                // Reject the connection
                qDebug("HttpListener: Too many incoming connections");
                QTcpSocket* socket=new QTcpSocket(this);
                socket->setSocketDescriptor(socketDescriptor);
                connect(socket, SIGNAL(disconnected()), socket, SLOT(deleteLater()));
                socket->write("HTTP/1.1 503 too many connections\r\nConnection: close\r\n\r\nToo many connections\r\n");
                socket->disconnectFromHost();
             }
          }

          poolMutex.unlock();
       }
       catch (...)
       {
          qCritical("HttpListener (%p): An uncatched exception occured in incomingConnection method", this);
          poolMutex.unlock();
          throw;
       }
    }

    if (!serviceAvailable)
    {
       if (!run)
       {
          close();
       }

       // Reject the connection
       qDebug("HttpListener: Not listening to new connections");
       QTcpSocket* socket=new QTcpSocket(this);
       socket->setSocketDescriptor(socketDescriptor);
       connect(socket, SIGNAL(disconnected()), socket, SLOT(deleteLater()));
       socket->write("HTTP/1.1 503 Service unavailable\r\nConnection: close\r\n\r\nService unavailable\r\n");
       socket->disconnectFromHost();
    }
}
