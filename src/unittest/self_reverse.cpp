/**
 * unittest/self_reverse.cpp: test cases for xg edges in the start/end format.
 */

#include "catch.hpp"
#include "../handle.hpp"
#include "../vg.hpp"
#include "../xg.hpp"
#include "../utility.hpp"

namespace vg {
    namespace unittest {
        
        using namespace std;
        
        TEST_CASE("Make a graph with lots of self reversing edges", "[self_reverse]") {
            
            // Build a graph with lots of self-reversing edges and multiple edge types
            const string graph_json = R"(
            
            {
                "node": [
                         {"id": 1, "sequence": "GATTAC"},
                         {"id": 2, "sequence": "A"},
                         {"id": 3, "sequence": "AAAAA"},
                         {"id": 4, "sequence": "CATTAG"},
                         {"id": 5, "sequence": "TAGTAG"},
                         {"id": 6, "sequence": "TAG"},
                         {"id": 7, "sequence": "AGATA"},
                         {"id": 8, "sequence": "TTT"}
                         ],
                "edge": [
                         {"from": 1, "to": 1, "from_start": true},
                         {"from": 1, "to": 2},
                         {"from": 3, "to": 2, "from_start": true, "to_end": true},
                         {"from": 3, "to": 3, "to_end": true},
                         {"from": 1, "to": 4},
                         {"from": 4, "to": 5, "to_end": true},
                         {"from": 5, "to": 6, "from_start": true},
                         {"from": 6, "to": 6},
                         {"from": 7, "to": 6, "from_start": true, "to_end": true},
                         {"from": 7, "to": 7, "to_end": true},
                         {"from": 7, "to": 8},
                         {"from": 8, "to": 8, "from_start": true, "to_end": true}
                         ]
            }
            
            )";
            
            // Make the Graph
            Graph g;
            json2pb(g, graph_json.c_str(), graph_json.size());
            
            // Pass it over to XG
            xg::XG index(g);
            
            SECTION("Can iterate through all the nodes in the graph and check edges") {
                REQUIRE(g.edge_size() == 12);
                for (int i = 0; i < g.node_size(); i++) {
                    const Node& n = g.node(i);
                    REQUIRE(n.id() != 0);
                }
                for (int64_t  i = 0; i < g.node_size(); i++) {
                    const Node& on_node = g.node(i);
                    handle_t node_handle = index.get_handle(on_node.id(), true);
                    
                    // make sure all the connections off the end_side of node is good
                    index.follow_edges(node_handle, true, [&](const handle_t& n){
                        id_t id = index.get_id(n);
                        if (on_node.id() == 1){
                            bool is_correct_id = (id == 2 || id == 4);
                            REQUIRE(is_correct_id == true);
                        }
                        if (on_node.id() == 2){
                            REQUIRE(id == 3);
                        }
                        if (on_node.id() == 3){
                            REQUIRE(id == 3);
                        }
                        if (on_node.id() == 4){
                            REQUIRE(id == 5);
                        }
                        if (on_node.id() == 5){
                            REQUIRE(id == 4);
                        }
                        if (on_node.id() == 6){
                            bool is_correct_id = (id == 6 || id == 7);
                            REQUIRE(is_correct_id == true);
                        }
                        if (on_node.id() == 7){
                            bool is_correct_id = (id == 7 || id == 8);
                            REQUIRE(is_correct_id == true);
                        }
                        if (on_node.id() == 8){
                            REQUIRE(id == 8);
                        }
                        return true;
                    });
                    
                    // make sure all the connections off the start_side of node is good
                    index.follow_edges(node_handle, false, [&](const handle_t& n){
                        id_t id = index.get_id(n);
                        if (on_node.id() == 1){
                            REQUIRE(id == 1);
                        }
                        if (on_node.id() == 2){
                            REQUIRE(id == 1);
                        }
                        if (on_node.id() == 3){
                            REQUIRE(id == 2);
                        }
                        if (on_node.id() == 4){
                            REQUIRE(id == 1);
                        }
                        if (on_node.id() == 5){
                            REQUIRE(id == 6);
                        }
                        if (on_node.id() == 6){
                            bool is_correct_id = (id == 6 || id == 5);
                            REQUIRE(is_correct_id == true);
                        }
                        if (on_node.id() == 7){
                            REQUIRE(id == 6);
                        }
                        if (on_node.id() == 8){
                            bool is_correct_id = (id == 7 || id == 8);
                            REQUIRE(is_correct_id == true);
                        }
                        return true;
                    });
                }
            }
        }
    }
}
