#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "header.h"

using namespace std;

uint16_t get_udp_packet_size(meta_udp_packet &msg) {
    uint16_t size = 0;

    switch (msg.data_type) {
        case 0: // int
            size = 5;
            break;
        case 1: // short_real
            size = 2;
            break;
        case 2: // float
            size = 6;
            break;
        case 3: { // string
            size = 0;
            while (size < 1500 && msg.data[size] != '\0') {
                ++size;
            }
            if (size == 1500 && msg.data[1499] != '\0') {
                size = 1500;
            }
            break;
        }
        default:
            size = 0; 
            break;
    }
    return size;
}

void handle_new_subscriber_request(int subscriberfd, sockaddr_in cli_addr) {
    setsockopt(subscriberfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    char subscriber_id[100];
    collect_socket_data(subscriber_id, 100, subscriberfd);

    string id(subscriber_id);
    subscribers[subscriberfd] = subscriber_t(subscriberfd, id, cli_addr.sin_addr.s_addr, ntohs(cli_addr.sin_port));

    if (online_subscribers.count(id)) {
        printf("Client %s already connected.\n", subscriber_id);
        close(subscriberfd);
        return;
    }

    printf("New client %s connected from %s:%d.\n", subscriber_id, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
    online_subscribers.insert(id);
}

void handle_server_input(pollfd *fds, const int &tcpfd, const int &udpfd) {
    char buffer[2048];
    memset(buffer, 0, sizeof(buffer));

    fgets(buffer, 2048, stdin);

    if (strncmp(buffer, "exit", 4) == 0) {
        delete fds;
        close(tcpfd);
        close(udpfd);
        for (auto &subscriber : subscribers) {
            close(subscriber.first);
        }
        exit(EXIT_SUCCESS);
    }

    cerr << "Unknown command" << '\n';
}

vector<string> split_string(const string &input, char delimiter) {
    vector<string> result;
    string current;
    for (char c : input) {
        if (c == delimiter) {
            if (!current.empty()) {
                result.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        result.push_back(current);
    }
    return result;
}

bool check_topic_fit(const string &pattern, const string &topic) {
    if (pattern == "*" || pattern == topic) {
        return true;
    }

    vector<string> pattern_parts = split_string(pattern, '/');
    vector<string> topic_parts = split_string(topic, '/');

    size_t pattern_len = pattern_parts.size();
    size_t topic_len = topic_parts.size();

    vector<vector<bool>> match_state(topic_len + 1, vector<bool>(pattern_len + 1, false));
    match_state[0][0] = true;

    for (size_t topic_idx = 0; topic_idx <= topic_len; ++topic_idx) {
        for (size_t pattern_idx = 0; pattern_idx < pattern_len; ++pattern_idx) {
            if (match_state[topic_idx][pattern_idx]) {
                if (pattern_parts[pattern_idx] == "*") {
                    match_state[topic_idx][pattern_idx + 1] = true;
                    if (topic_idx < topic_len) {
                        match_state[topic_idx + 1][pattern_idx] = true;
                    }
                } else if (topic_idx < topic_len) {
                    bool segment_match = (pattern_parts[pattern_idx] == topic_parts[topic_idx] ||
                                         pattern_parts[pattern_idx] == "+");
                    match_state[topic_idx + 1][pattern_idx + 1] = segment_match;
                }
            }
        }
    }

    return match_state[topic_len][pattern_len];
}

void handle_udp_client_request(pollfd *fds, int tcpfd, int udpfd) {
    meta_udp_packet meta_packet;
    sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    memset(&meta_packet, 0, sizeof(meta_packet));
    ssize_t bytes_received = recvfrom(udpfd, (char *)(&meta_packet.topic), sizeof(meta_packet.topic) + sizeof(meta_packet.data_type) + sizeof(meta_packet.data), 0, (sockaddr *)&cli_addr, &cli_len);
    if (bytes_received == -1) {
        perror("Failed to receive UDP data");
        delete fds;
        close(tcpfd);
        close(udpfd);
        for (auto &subscriber : subscribers) {
            close(subscriber.first);
        }
        return;
    }

    meta_packet.ip = cli_addr.sin_addr.s_addr;
    meta_packet.port = ntohs(cli_addr.sin_port);

    char topic[51];
    memcpy(topic, meta_packet.topic, 50);
    topic[50] = '\0';

    uint16_t nth_ord_len = htons(get_udp_packet_size(meta_packet));
    string received_topic = string(meta_packet.topic);

    for (auto &[subscriber, subscriber_topics] : subscriber_topics) {
        for (auto &subscriber_topic : subscriber_topics) {
            if (check_topic_fit(subscriber_topic, received_topic)) {
                if (online_subscribers.contains(subscriber.id)) {
                    transmit_full_data(&nth_ord_len, sizeof(uint16_t), subscriber.sockfd);
                    transmit_full_data(&meta_packet, sizeof(meta_udp_packet) - 1500 + get_udp_packet_size(meta_packet), subscriber.sockfd);
                }

                break;
            }
        }
    }
}

void handle_subscriber(pollfd *fds, int &nr_clients, int n, const int &tcpfd, const int &udpfd) {
    auto process_client_data = [&](int sockfd, subscriber_t &sub, pollfd *fds, int idx, int &client_count, char *out_buffer) -> bool {
        uint16_t data_size;
        char temp_buffer[2048] = {0};

        collect_socket_data(&data_size, sizeof(uint16_t), sockfd);
        int result = collect_socket_data(temp_buffer, ntohs(data_size), sockfd);
        sub = subscribers[sockfd];

        strncpy(out_buffer, temp_buffer, 2048);

        bool connection_lost = (result <= 0);
        bool received_exit = (strncmp(temp_buffer, "exit", 4) == 0);

        if (connection_lost || received_exit) {
            cout << "Client " << sub.id << " disconnected." << '\n';

            online_subscribers.erase(sub.id);
            close(sockfd);

            pollfd &last_client_fd = fds[client_count - 1];
            fds[idx] = last_client_fd;
            memset(&last_client_fd, 0, sizeof(pollfd));
            client_count--;

            return false;
        }
        return true;
    };

    int sockfd = fds[n].fd;
    subscriber_t subscriber;
    char buff[2048] = {0};

    if (!process_client_data(sockfd, subscriber, fds, n, nr_clients, buff)) {
        return;
    }

    if (strncmp(buff, "subscribe", 9) == 0) {
        subscriber_topics[subscriber].insert(string(buff + 10));
    } else if (strncmp(buff, "unsubscribe", 11) == 0) {
        subscriber_topics[subscriber].erase(string(buff + 12));
    }
}

void run_server(int s1,int s2) {
    int cap = 3;
    std::vector<pollfd> fds(cap);

    fds[0].fd = s1;
    fds[0].events = POLLIN;

    listen(s1, 100);

    fds[1].fd = s2;
    fds[2].fd = STDIN_FILENO;
    fds[1].events = fds[2].events= POLLIN;

    int count = cap;

    while (true) {
        poll(fds.data(), count, -1);

        for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
            if (!(fds[i].revents & POLLIN)) {
                continue;
            }

            switch (fds[i].fd) {
                case -1:
                    break;
                case 0: // STDIN_FILENO
                    handle_server_input(fds.data(), s1, s2);
                    break;
                default:
                    if (fds[i].fd == s1) {
                        sockaddr_in cli_addr;
                        socklen_t cli_len = sizeof(cli_addr);
                        int subscriberfd = accept(s1, (sockaddr *)&cli_addr, &cli_len);
                        if (subscriberfd == -1) {
                            perror("Failed to accept connection");
                            continue;
                        }

                        // double the size
                        if (count == cap) {
                            cap <<= 1;
                            fds.resize(cap);
                        }

                        // add new subscriber
                        fds[count].fd = subscriberfd;
                        fds[count].events = POLLIN;
                        count++;

                        handle_new_subscriber_request(subscriberfd, cli_addr);
                    } else if (fds[i].fd == s2) {
                        handle_udp_client_request(fds.data(), s1, s2);
                    } else {
                        handle_subscriber(fds.data(), count, i, s1, s2);
                    }
                    break;
            }
        }
    }
}

int initialize_tcp_socket(sockaddr_in &server_config) {
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket == -1) {
        perror("Failed to create TCP socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0
        || setsockopt(tcp_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
            perror("setsockopt");
            close(tcp_socket);
            exit(EXIT_FAILURE);
    }

    if (bind(tcp_socket, (sockaddr *)&server_config, sizeof(server_config)) == -1) {
        perror("Failed to bind TCP socket");
        close(tcp_socket);
        exit(EXIT_FAILURE);
    }

    return tcp_socket;
}

int initialize_udp_socket(sockaddr_in &server_config) {
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);

    if (setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0) {
        perror("Failed to set SO_REUSEADDR on UDP socket");
        close(udp_socket);
        exit(EXIT_FAILURE);
    }

    if (bind(udp_socket, (sockaddr *)&server_config, sizeof(server_config)) < 0) {
        perror("Failed to bind UDP socket");
        close(udp_socket);
        exit(EXIT_FAILURE);
    }
    return udp_socket;
}

int main(int argc, char *argv[]) {
    if (argc == 2) {
        setvbuf(stdout, nullptr, _IONBF, BUFSIZ);

        uint16_t server_port;
        sscanf(argv[1], "%hu", &server_port);

        sockaddr_in server_address;
        auto address_size = sizeof(sockaddr_in);
        memset(&server_address, 0, address_size);

        server_address.sin_family = AF_INET;
        server_address.sin_port = htons(server_port);
        server_address.sin_addr.s_addr = INADDR_ANY;

        int tcp_socket = initialize_tcp_socket(server_address);
        int udp_socket = initialize_udp_socket(server_address);

        run_server(tcp_socket, udp_socket);
        return 0;
    }
    return 1;
}