#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <sstream>

#include "header.h"

void print_udp_packet(meta_udp_packet &packet) {
	char ip[16];
	if (inet_ntop(AF_INET, &packet.ip, ip, 16) == NULL) {
		perror("inet_ntop");
		strcpy(ip, "0.0.0.0");
	}
	std::stringstream ss;
	ss << ip << ":" << ntohs(packet.port) << " - ";
	string output = ss.str();

    char topic[51];
    strncpy(topic, packet.topic, 50);
    topic[50] = '\0';
    output += string(topic) + " - ";

    switch (packet.data_type) {
        case 0: { // INT
            int val = ntohl(*(uint32_t *)(packet.data + 1));
            output += "INT - " + (string)(packet.data[0] == 1 && val != 0 ? "-" : "") + to_string(val);
            break;
        }
        case 1: { // SHORT_REAL
            int val = ntohs(*(uint16_t *)packet.data);
            string num = to_string(val);
            if (num.length() >= 2) {
                num.insert(num.length() - 2, ".");
            }
            output += "SHORT_REAL - " + num;
            break;
        }
        case 2: { // FLOAT
            int val = ntohl(*(uint32_t *)(packet.data + 1));
            int power = packet.data[5];
            string num = to_string(val);
            while (num.length() <= power) {
                num = "0" + num;
            }
            if (power > 0) {
                num.insert(num.length() - power, ".");
            }
            output += "FLOAT - " + (string)(packet.data[0] == 1 ? "-" : "") + num;
            break;
        }
        case 3: { // STRING
            output += "STRING - " + string(packet.data);
            break;
        }
    }

    cout << output << endl;
}

void handle_server_messages(int sockfd) {
    uint16_t len;

    if (collect_socket_data(&len, sizeof(uint16_t), sockfd) < 0) {
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    len = ntohs(len);

    meta_udp_packet packet;
    memset(&packet, 0, sizeof(meta_udp_packet));

    int result = collect_socket_data(&packet, sizeof(meta_udp_packet) - 1500 + len, sockfd);
    if (result <= 0) {
        close(sockfd);
		if (result == 0) {
        	exit(EXIT_SUCCESS);
		}
		exit(EXIT_FAILURE);
    }

    print_udp_packet(packet);
}

void handle_commands(int sockfd, string subscriber_id) {
    char buff[2048];
    fgets(buff, sizeof(buff), stdin);
    buff[strcspn(buff, "\n")] = 0;

    uint16_t len = strlen(buff) + 1;
    uint16_t len_n = htons(len);

    if (transmit_full_data(&len_n, sizeof(uint16_t), sockfd) < 0 || transmit_full_data(buff, len, sockfd) < 0) {
        perror("transmit_full_data");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    if (strncmp(buff, "subscribe", 9) == 0) {
        std::cout << "Subscribed to topic " << buff + 10 << std::endl;
    } else if (strncmp(buff, "unsubscribe", 11) == 0) {
        std::cout << "Unsubscribed from topic " << buff + 12 << std::endl;
    } else if (strncmp(buff, "exit", 4) == 0) {
        close(sockfd);
        exit(0);
    }
}

void run_client(int sockfd, string subscriber_id) {
    pollfd fds[2];
    fds[0].fd = sockfd;
    fds[1].fd = STDIN_FILENO;
    fds[0].events = fds[1].events= POLLIN;

    do {
        poll(fds, 2, -1);
        if (fds[0].revents & POLLIN) {
            handle_server_messages(sockfd);
        }
        if (fds[1].revents & POLLIN) {
            handle_commands(sockfd, subscriber_id);
        }
    } while (true);
}

int main(int argc, char *argv[]) {
    if (argc == 4) {
        setvbuf(stdout, NULL, _IONBF, BUFSIZ);
		int sockfd = socket(AF_INET, SOCK_STREAM, 0);

		if (sockfd < 0) {
			perror("socket");
			return 1;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int)) < 0
			|| setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)) < 0) {
			perror("setsockopt");
			close(sockfd);
			return 1;
		}

		uint16_t port;
        if (sscanf(argv[3], "%hu", &port) == 1) {
			sockaddr_in serv_addr;
			serv_addr.sin_family = AF_INET;
			serv_addr.sin_port = htons(port);

			if (inet_pton(AF_INET, argv[2], &serv_addr.sin_addr.s_addr) < 0) {
				close(sockfd);
				return 1;
			}

			if (connect(sockfd, (sockaddr *)&serv_addr, sizeof(sockaddr_in)) < 0) {
				close(sockfd);
				return 1;
			}

			if (transmit_full_data(argv[1], 100, sockfd) < 0) {
				close(sockfd);
				return 1;
			}

			run_client(sockfd, (string)argv[1]);
			close(sockfd);
			return 0;
		}
    }
    return 1;
}