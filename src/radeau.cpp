#include <iostream>
#include <vector>
#include <cassert>

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
    size_t last_log_index;
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

struct Log {
    int term;
    std::string log;
};

struct Server {
    int id;  // ids start at 0
    std::vector<Server*> others;
    ServerState state {ServerState::Follower};
    float last_heartbeat_from_leader {0.0f};
    
    // persistent storage
    int term {0};
    int voted_for; // 0 indicates no vote
    std::vector<Log> log;

    // volatile state

    // volatide leader-only state
};

#define SERVER_COUNT 5

#define ELECTION_TIMEOUT 3.0f

RPCResponse server_send_rpc(Server& server, RPC rpc) {
    switch (rpc.type) {
        case RPCType::RequestVote: {
            RequestVoteResponse response {};
            if (server.term > rpc.request_vote.term) {
                response.term = server.term;
                response.vote_granted = false;
            } else if ((server.voted_for == 0 || server.voted_for == rpc.request_vote.candidate_id) && (rpc.request_vote.last_log_term >= server.log.back().term) && (rpc.request_vote.last_log_index >= (server.log.size() + 1))) {
                response.term = server.term;
                response.vote_granted = true;
            } else {
                response.term = server.term;
                response.vote_granted = false;
            }
            return RPCResponse {
                RPCType::RequestVote,
                response,
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
        int vote_count = 1;
        for (Server* other: server.others) {
            RPCResponse response = server_send_rpc(*other, RPC {
                RPCType::RequestVote,
                RequestVote {server.term, server.id, server.log.size() + 1, server.log.back().term},
            });
            assert(response.type == RPCType::RequestVote);
            RequestVoteResponse request_vote_response = response.request_vote_response;
            if (request_vote_response.vote_granted) {
                assert(request_vote_response.term <= server.term);
                vote_count++;
            }
            if (vote_count * 2 >= SERVER_COUNT) {
                // majority of votes, become the leader
                server.state = ServerState::Leader;
            }
        }
    }
}

int main() {
    std::vector<Server> servers(SERVER_COUNT);

    for (int i = 0; i < SERVER_COUNT; ++i) {
        servers[i].id = i + 1; // ids start at one
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
