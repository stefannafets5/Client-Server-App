#Copyright - Springer Robert Stefan 2025

    In the main function, the server initializes TCP and UDP sockets on a given port
    and processes TCP requests. The client connects to the server via TCP, sends a
    unique ID, and processes subscription commands or UDP messages. Both use poll to
    monitor stdin and sockets.

    Subscription: The client sends subscribe/unsubscribe commands to subscribe to
    topics. The server stores subscriptions and sends relevant UDP messages.

- "meta_udp_packet" holds IP, port, topic, data type, and content.
- "Subscriber_t" describes a client with socket, ID, IP, and port.
- "subscribers" maps socket to Subscriber_t.
- "online_subscribers" keeps track of active clients.
- "subscriber_topics" associates clients with topics.
- "check_topic_fit" checks topic matching with * and +.
- "get_udp_packet_size" calculates the UDP data size based on type
(int, short_real, float, string).

- "handle_new_subscriber_request" accepts TCP connections, checks IDs, and
adds clients to subscribers and online_subscribers.

- "handle_udp_client_request" receives UDP messages and forwards them to
clients subscribed to the relevant topics.

- "handle_subscriber" processes TCP requests (subscribe/unsubscribe) and disconnections.

    Utility functions:

- "initialize_tcp_socket" and "initialize_udp_socket" configure sockets with
SO_REUSEADDR and TCP_NODELAY.

- "collect_socket_data" and "transmit_full_data" ensure complete data transfer.
- "print_udp_packet" displays UDP messages, formatting data based on type.