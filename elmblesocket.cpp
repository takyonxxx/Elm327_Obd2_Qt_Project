#include "elmblesocket.h"
#include <QDebug>


ElmBleSocket::ElmBleSocket(QObject *parent):localDevice(new QBluetoothLocalDevice)
{

}

ElmBleSocket::~ElmBleSocket()
{
    delete localDevice;
    delete discoveryAgent;
    delete socket;
}

void ElmBleSocket::run()
{
}

void ElmBleSocket::scanBle()
{
    qRegisterMetaType<QBluetoothDeviceInfo>("QBluetoothDeviceInfo");
    qRegisterMetaType<QString>("QString&");

    QMutexLocker locker(&m_mutex);

    if (localDevice->isValid())
    {
        //Turn BT on
        localDevice->powerOn();

        // Make it visible to others
        localDevice->setHostMode(QBluetoothLocalDevice::HostDiscoverable);
        discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
        discoveryAgent->setLowEnergyDiscoveryTimeout(5000);

        connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this, &ElmBleSocket::addDevice);
        connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished, this, &ElmBleSocket::scanFinished);
        connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::canceled, this, &ElmBleSocket::scanFinished);
        connect(discoveryAgent, static_cast<void (QBluetoothDeviceDiscoveryAgent::*)
                (QBluetoothDeviceDiscoveryAgent::Error)>(&QBluetoothDeviceDiscoveryAgent::error), this, &ElmBleSocket::scanError);

        if(discoveryAgent)
        {
            discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
            QString msg{};
            msg.append("Scanning bluetooth devices");
            emit stateChanged(msg);
        }
    }
}

void ElmBleSocket::handleDiscoveryTimeout()
{
    discoveryAgent->stop();
}

void ElmBleSocket::startScan()
{
    m_scanTimer.setSingleShot(true);
    m_scanTimer.setInterval(5000);
    connect(&m_scanTimer, &QTimer::timeout, this, &ElmBleSocket::handleDiscoveryTimeout);
    m_scanTimer.start();

    QFuture<void> t1 = QtConcurrent::run(this, &ElmBleSocket::scanBle);
    t1.waitForFinished();
}

void ElmBleSocket::stopScan()
{
    if(discoveryAgent)
    {
        discoveryAgent->stop();
    }
}

void ElmBleSocket::connectBle(const QBluetoothAddress & address)
{
    QString msg{};
    msg.append("Connecting to bluetooth : " + QString(address.toString()));
    emit stateChanged(msg);
    QCoreApplication::processEvents();

    if (localDevice->pairingStatus(address)== QBluetoothLocalDevice::Paired)
    {
        emit stateChanged(QString(address.toString()) + " Pairing is allready done");
    }
    else
    {
        emit bleDisconnected();
        emit stateChanged(QString(address.toString()) + " Not paired. Please do pairing first!");
        return;
    }

    socket = new QBluetoothSocket(QBluetoothServiceInfo::RfcommProtocol);
    connect(socket, &QBluetoothSocket::connected, this, &ElmBleSocket::connected);
    connect(socket,  &QBluetoothSocket::disconnected, this, &ElmBleSocket::disconnected);
    connect(socket, SIGNAL(error(QBluetoothSocket::SocketError)), this, SLOT(socketError(QBluetoothSocket::SocketError)));
    connect(socket,&QBluetoothSocket::stateChanged,this,&ElmBleSocket::bleStateChanged);
    connect(socket,  &QBluetoothSocket::readyRead, this, &ElmBleSocket::readyRead);
    socket->connectToService(address, QBluetoothUuid(QBluetoothUuid::SerialPort), QIODevice::ReadWrite);
    //socket->connectToService(address, QBluetoothUuid(QString("00001101-0000-1000-8000-00805F9B34FB")), QIODevice::ReadWrite);
    socket->open(QIODevice::ReadWrite);
}

void ElmBleSocket::disconnectBle()
{
    if(socket->isOpen())
    {
        socket->disconnectFromService();
        socket->deleteLater();
    }
}

bool ElmBleSocket::isConnected()
{
    return m_connected;
}


void ElmBleSocket::connected()
{
    m_connected = true;
    emit bleConnected();
}

void ElmBleSocket::disconnected()
{
    socket->deleteLater();
    m_connected = false;
    emit bleDisconnected();
}


void ElmBleSocket::socketError(QBluetoothSocket::SocketError error)
{
    auto errorString = socket->errorString();
    emit stateChanged(errorString);
}

void ElmBleSocket::scanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    auto scanError = discoveryAgent->errorString();
    emit stateChanged(scanError);
}

void ElmBleSocket::addDevice(const QBluetoothDeviceInfo &info)
{
    /*if(info.name().toUpper().contains("ECU") || info.name().toUpper().contains("OBD")
            || info.name().toUpper().contains("ELM") || info.name().toUpper().contains("SCAN"))
    {
        discoveryAgent->stop();
        emit addBleDevice(info.address(), info.name());
    }*/
    emit addBleDevice(info.address(), info.name());
}

void ElmBleSocket::scanFinished()
{
    QString msg{};
    msg.append("Ble Discovery finished.");
    emit stateChanged(msg);

    auto listOfDevices = discoveryAgent->discoveredDevices();

    for (int i = 0; i < listOfDevices.size(); i++)
    {
        auto deviceName = listOfDevices.at(i).name().toUpper();
        //Q_OS_IOS / MAC do not need here cuz the run is on android device, delete later
        //We do not find galileo device on MacBook

#if defined (Q_OS_IOS) || defined (Q_OS_MAC)
        // On MacOS and iOS we get no access to device address,
        // only unique UUIDs generated by Core Bluetooth.
        if(deviceName.contains("OBD") || deviceName.contains("ECU") || deviceName.contains("ELM") || deviceName.contains("SCAN"))
        {
            QString msg{};
            msg.append(listOfDevices.at(i).name().trimmed()
                       + " ( " + listOfDevices.at(i).deviceUuid().toString().trimmed() + ")");
            emit stateChanged(msg);
        }

#else
        if(deviceName.contains("OBD") || deviceName.contains("ECU") || deviceName.contains("ELM") || deviceName.contains("SCAN"))
        {
            QString msg{};
            msg.append(listOfDevices.at(i).name().trimmed()
                       + " (" + listOfDevices.at(i).address().toString().trimmed() + ")");
            emit stateChanged(msg);
        }
#endif
    }
}

bool ElmBleSocket::send(const QString &string)
{
    if(socket->isOpen())
    {
        connect(socket,&QBluetoothSocket::readyRead,this,&ElmBleSocket::readyRead);

        QByteArray dataToSend = string.toUtf8();

        if (string.isEmpty())
        {
            // If toWrite is empty then just send a CR char.
            dataToSend += ('\r');
        }
        else
        {
            // Check for CR at end.
            if (dataToSend[dataToSend.size()] != '\r')
                dataToSend += '\r';
        }

        socket->write(dataToSend);
        return true;
    }
    else
        return false;
}

bool ElmBleSocket::sendAsync(const QString &command)
{
    if(socket->isOpen())
    {
        QByteArray dataToSend = command.toUtf8();

        if (command.isEmpty())
        {
            // If toWrite is empty then just send a CR char.
            dataToSend += ('\r');
        }
        else
        {
            // Check for CR at end.
            if (dataToSend[dataToSend.size()] != '\r')
                dataToSend += '\r';
        }

        socket->write(dataToSend);
        return true;
    }
    else
        return false;
}


QString ElmBleSocket::checkData()
{
    QString strData{};

    while (socket->bytesAvailable() > 0)
    {
        QByteArray data = socket->readAll();
        byteblock += data;

        strData = QString::fromStdString(byteblock.toStdString());
        if(strData.contains("\r"))
        {
            byteblock.clear();
            strData.remove("\r");
            strData.remove(">");

            strData = strData.trimmed()
                    .simplified()
                    .remove(QRegExp("[\\n\\t\\r]"))
                    .remove(QRegExp("[^a-zA-Z0-9]+"));

            // Some of these look like errors that ought to be handled..
            strData.replace("OK","");
            strData.replace("?","");
            strData.replace(",","");
            emit dataReceived(strData);
            return strData;
        }
    }
    return strData;
}

QString ElmBleSocket::readData(const QString &command)
{
    QString strData{};

    if(sendAsync(command))
    {
        //if (socket->waitForReadyRead(-1)) //not implemented
        {
            while (socket->bytesAvailable() > 0)
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents);

                QByteArray data = socket->readAll();
                byteblock += data;

                strData = QString::fromStdString(byteblock.toStdString());

                if(strData.contains("\r"))
                {
                    byteblock.clear();
                    strData.remove("\r");
                    strData.remove(">");

                    strData = strData.trimmed()
                            .simplified()
                            .remove(QRegExp("[\\n\\t\\r]"))
                            .remove(QRegExp("[^a-zA-Z0-9]+"));

                    // Some of these look like errors that ought to be handled..
                    strData.replace("?","");
                    strData.replace(",","");
                    if(!strData.isEmpty())
                    {
                        if(strData.contains("SEARCHING"))
                        {
                            QCoreApplication::processEvents();
                            return  checkData();
                        }
                    }

                    emit dataReceived(strData);
                    return strData;
                }
            }
        }
    }
    return strData;
}

void ElmBleSocket::readyRead()
{
    QString strData{};
    QByteArray data = socket->readAll();
    byteblock += data;

    strData = QString::fromStdString(byteblock.toStdString());

    if(strData.contains("\r"))
    {
        if(!strData.isEmpty())
        {
            byteblock.clear();
            strData.remove("\r");
            strData.remove(">");

            strData = strData.trimmed()
                    .simplified()
                    .remove(QRegExp("[\\n\\t\\r]"))
                    .remove(QRegExp("[^a-zA-Z0-9]+"));

            // Some of these look like errors that ought to be handled..
            strData.replace("?","");
            strData.replace(",","");
            if(!strData.isEmpty())
            {
                disconnect(socket,&QBluetoothSocket::readyRead,this,&ElmBleSocket::readyRead);
                emit dataReceived(strData);
            }
        }
    }
}

QString ElmBleSocket::statetoString(QBluetoothSocket::SocketState socketState)
{
    QString statestring;
    switch(socketState)
    {
    case QBluetoothSocket::UnconnectedState :
    {
        statestring="The Ble socket is not connected";
        emit disconnected();
    }
        break;
    case QBluetoothSocket::ConnectingState : statestring="The socket has started establishing a connection";
        break;
    case QBluetoothSocket::ConnectedState : statestring="Connection is established";
        break;
    case QBluetoothSocket::BoundState : statestring="The socket is bound to an address and port";
        break;
    case QBluetoothSocket::ClosingState : statestring="The socket is about to close";
        break;
    case QBluetoothSocket::ListeningState : statestring="Listening state";
        break;
    default: statestring="Unknown state";
        break;
    }
    return statestring;
}

void ElmBleSocket::bleStateChanged(QBluetoothSocket::SocketState socketState)
{
    QString state(statetoString(socketState).toStdString().c_str());
    emit stateChanged(state);
}
