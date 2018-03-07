#include "solenoid.h"

#include <QSerialPortInfo>

#include <cstdint>

Solenoid::Solenoid()
    : Controller(true, true) {
    // For convenience, attempt auto-detect connection at launch
    attempt_connection();
}

Solenoid::Solenoid(
    const QString &serial_port,
    QSerialPort::BaudRate baud_rate
)
    : Controller(true, true) {
    attempt_connection(serial_port, baud_rate);
}

Solenoid::~Solenoid() {
    // Release connection if it is still open
    if (m_serial.isOpen()) { m_serial.close(); }
}

void Solenoid::attempt_connection(
    const QString &serial_port,
    QSerialPort::BaudRate baud_rate
) {
    Q_EMIT serial_status(SerialStatus::CONNECTING);
    if (serial_port.isEmpty()) {
        // Auto-detect Arduino port
        QSerialPortInfo port_to_use;
        bool found_port = false;
        auto ports = QSerialPortInfo::availablePorts();
        for (auto &port : ports) {
            if (
                !port.isBusy() && (port.description().contains("Arduino") ||
                                   port.manufacturer().contains("Arduino"))
                ) {
                port_to_use = port;
                found_port = true;
                break;
            }
        }
        if (!found_port) {
            fatal() << "No Arduino port specified, failed to autodetect port";
            Q_EMIT serial_status(SerialStatus::DISCONNECTED);
            return;
        }
        log() << "Auto-detected Arduino port: " << port_to_use.portName();
        // Use our auto-detected port info
        m_serial.setPort(port_to_use);
    } else {
        // Set the provided port name
        m_serial.setPortName(serial_port);
    }
    m_serial.setBaudRate(baud_rate);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);
    if (!m_serial.open(QIODevice::ReadWrite)) {
        fatal() << "Failed to open serial port: " << m_serial.portName();
        Q_EMIT serial_status(SerialStatus::DISCONNECTED);
    } else {
        log() << "Opened serial port: " << m_serial.portName();
        connect(&m_serial, &QSerialPort::readyRead, this, &Solenoid::readSerial);
        Q_EMIT serial_status(SerialStatus::CONNECTED);
    }
}

void Solenoid::attempt_disconnect() {
    if (m_serial.isOpen()) {
        log() << "Disconnecting serial port";
        m_serial.close();
        Q_EMIT serial_status(SerialStatus::DISCONNECTED);
    }
}

void Solenoid::readSerial() {
    QByteArray data = m_serial.readAll();
    std::string msg = data.toStdString();
    if (!msg.empty()) {
        Q_EMIT serialRead(msg);
    }
}

Vector2i Solenoid::to_vector2i(Dir dir) {
    constexpr int power = 255;
    switch (dir) {
        case Dir::UP:
            return {0, power};
        case Dir::DOWN:
            return {0, -power};
        case Dir::RIGHT:
            return {power, 0};
        case Dir::LEFT:
            return {-power, 0};
        default:
            return {0, 0};
    }
}

void Solenoid::__move_delegate(Vector2i dir, int time) {
#ifndef NDEBUG
    debug() << "Moving Solenoid controller";
    debug() << "Attempting to move " << dir;
#endif
    if (!m_serial.isOpen()) {
        fatal() << "Failed to execute movement: serial port not open";
        return;
    }
#ifndef NDEBUG
    debug() << "Sending message:";
    debug() << dir << " : " << time;
#endif
    m_serial.write(encode_message(dir, time));
    if (!m_serial.waitForBytesWritten(200)) {
        fatal() << "Failed to execute movement: write timed out";
    } else {
        log() << "Movement sent";
    }
}

bool Solenoid::is_connected() const {
    return m_serial.isOpen();
}

const QSerialPort &Solenoid::serial_port() const {
    return m_serial;
}

QByteArray Solenoid::encode_message(Vector2i dir, int time) {
    shrink_into<int16_t> s;
    vector2d<int16_t> short_vec(s(dir.x()), s(dir.y()));
    char raw[5];
    *reinterpret_cast<int16_t *>(raw) = short_vec.x();
    *reinterpret_cast<int16_t *>(raw + 2) = short_vec.y();
    raw[4] = shrink_into<uint8_t>()(time);
    QByteArray data(raw, 5);
    return data;
}
