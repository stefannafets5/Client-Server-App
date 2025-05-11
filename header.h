#ifndef __COMMON_H__
#define __COMMON_H__

#include <sys/socket.h>
#include <sys/types.h>
#include <string>
#include <unordered_map>
#include <unordered_set>

using namespace std;

#define LOCALHOST "127.0.0.1"

int flag = 1;

typedef struct {
    uint32_t ip;
    uint16_t port;
    char topic[50];
    uint8_t data_type;
    char data[1500];
}meta_udp_packet;

struct subscriber_t {
    int sockfd = -1;
    uint32_t ip = 0;
    uint16_t port = 0;
    string id = "";

    subscriber_t() {
        sockfd = -1;
        port = 0;
        ip = 0;
        id = "";
    }

    subscriber_t(int socket, string identifier, uint32_t addr, uint16_t port)
        : sockfd(socket),
          id(identifier),
          ip(addr),
          port(port) {
    }

    bool operator==(const subscriber_t &other) const {
        return id.compare(other.id) == 0;
    }

    struct hasher {
        size_t operator()(const subscriber_t &client) const noexcept {
            return std::hash<std::string>{}(client.id);
        }
    };
};

unordered_map<int, subscriber_t> subscribers;

int collect_socket_data(void *data_buffer, size_t total_bytes, int socket_fd) {
    size_t accumulated_bytes = 0;
    size_t remaining_bytes = total_bytes;
    char *target_buffer = static_cast<char *>(data_buffer);

    do {
        ssize_t current_bytes = recv(socket_fd, target_buffer + accumulated_bytes, remaining_bytes, 0);
        if (current_bytes < 0) {
            return -1;
        }
        if (current_bytes == 0) {
            break;
        }

        accumulated_bytes += current_bytes;
        remaining_bytes -= current_bytes;
    } while (remaining_bytes > 0);

    return accumulated_bytes;
}

unordered_map<subscriber_t, unordered_set<string>, subscriber_t::hasher> subscriber_topics;

int transmit_full_data(void *data_buffer, size_t total_bytes, int socket_fd) {
    size_t sent_bytes = 0;
    size_t bytes_to_send = total_bytes;
    char *target_buffer = static_cast<char *>(data_buffer);

    do {
        ssize_t current_sent = send(socket_fd, target_buffer + sent_bytes, bytes_to_send, 0);
        if (current_sent < 0) {
            return -1;
        }
        if (current_sent == 0) {
            break;
        }

        sent_bytes += current_sent;
        bytes_to_send -= current_sent;
    } while (bytes_to_send > 0);

    return sent_bytes;
}

unordered_set<string> online_subscribers;

#endif