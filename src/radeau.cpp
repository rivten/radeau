#include <iostream>
#include <vector>

enum class ServerState {
    Leader,
    Follower,
    Candidate,
};

enum class RPCType {
    RequestVote,
};

struct RequestVote {
    int term;
    int candidate_id;
    int last_log_index;
    int last_log_term;
};

struct RequestVoteResponse {
    int term;
    bool vote_granted;
};

struct RPC {
    RPCType type;
    union {
        RequestVote request_vote;
    };
};

struct RPCResponse {
    RPCType type;
    union {
        RequestVoteResponse request_vote_response;
    };
};

struct Server {
    int id;
    std::vector<Server*> others;
    ServerState state {ServerState::Follower};
    float last_heartbeat_from_leader {0.0f};
    
    // persistent storage
    int term {0};
    int voted_for;
    std::vector<std::string> log;

    // volatile state

    // volatide leader-only state
};

#define SERVER_COUNT 5

#define ELECTION_TIMEOUT 3.0f

RPCResponse server_send_rpc(Server& server, RPC rpc) {
    switch (rpc.type) {
        case RPCType::RequestVote: {
            return RPCResponse {
                RPCType::RequestVote,
                RequestVoteResponse {
                    server.term,
                    true
                }
            };
        } break;
    }
    return RPCResponse {
        RPCType::RequestVote,
        RequestVoteResponse {
            server.term,
            true
        }
    };
}

void update_server(Server& server, float dt) {
    server.last_heartbeat_from_leader += dt;
    if (server.last_heartbeat_from_leader >= ELECTION_TIMEOUT)
    {
        // start the election
        server.term++;
        server.voted_for = server.id;
        server.state = ServerState::Candidate;
        for (Server* other: server.others) {
            server_send_rpc(*other, RPC {
                RPCType::RequestVote,
                RequestVote {server.term, server.id, 0, 0},
            });
        }
    }
}

int main() {
    std::vector<Server> servers(SERVER_COUNT);

    for (int i = 0; i < SERVER_COUNT; ++i) {
        servers[i].id = i;
        for (int j = 0; j < SERVER_COUNT; ++j) {
            if (i != j) {
                servers[i].others.push_back(&servers[j]);
            }
        }

    }

    while (true) {
        const float dt = 1.0f;
        for (Server& server: servers) {
            update_server(server, dt);
        }
    }

    std::cout << "hello\n";
    return 0;
}
