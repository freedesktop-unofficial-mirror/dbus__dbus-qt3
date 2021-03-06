#include <QtCore/QtCore>
#include <dbus/qdbus.h>

class Pong: public QObject
{
    Q_OBJECT
public slots:

    void ping(const QDBusMessage &msg)
    {
        QDBusMessage reply = QDBusMessage::methodReply(msg);
        reply << static_cast<QList<QVariant> >(msg);
        reply.setSignature(msg.signature());
        if (!msg.connection().send(reply))
            exit(1);
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QDBusConnection &con = QDBus::sessionBus();
    if (!con.isConnected())
        exit(1);

    if (con.busService()->requestName("org.kde.selftest", QDBusBusService::DoNotQueueName).isError())
        exit(2);

    Pong pong;
    con.registerObject("/org/kde/selftest", &pong, QDBusConnection::ExportSlots);

    printf("ready.\n");

    return app.exec();
}

#include "qpong.moc"
