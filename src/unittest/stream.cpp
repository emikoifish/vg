/// \file stream.cpp
///  
/// Unit tests for stream file external interface

#include "../stream/stream.hpp"
#include "../stream/protobuf_iterator.hpp"
#include "../stream/protobuf_emitter.hpp"

#include "vg.pb.h"

#include "catch.hpp"

#include <sstream>
#include <iostream>
#include <unordered_map>

namespace vg {
namespace unittest {
using namespace std;

TEST_CASE("Protobuf messages that are all default can be stored and retrieved", "[stream]") {
    stringstream datastream;
    
    // Write one empty message
    REQUIRE(stream::write<Graph>(datastream, 1, [](size_t i) {
       return Graph();
    }));
    stream::finish(datastream);
    
    // Look for it
    int seen = 0;
    stream::for_each<Graph>(datastream, [&](const Graph& item) {
        seen++;
    });
    
    // Make sure it comes back
    REQUIRE(seen == 1);
}

TEST_CASE("Protobuf messages can be written and read back", "[stream]") {
    stringstream datastream;
    
    // Define some functions to make and check fake Protobuf objects
    using message_t = Position;
    
    auto get_message = [&](size_t index) {
        message_t item;
        item.set_node_id(index);
#ifdef debug
        cerr << "Made item " << index << endl;
#endif
        return item;
    };
    
    size_t index_expected = 0;
    auto check_message = [&](const message_t& item) {
#ifdef debug
        cerr << "Read item " << item.node_id() << endl;
#endif
        REQUIRE(item.node_id() == index_expected);
        index_expected++;
    };
    
    // Serialize some objects
    REQUIRE(stream::write<message_t>(datastream, 10, get_message));
    stream::finish(datastream);
    
#ifdef debug
    // Dump the compressed data
    auto data = datastream.str();
    for (size_t i = 0; i < data.size(); i++) {
        ios state(nullptr);
        state.copyfmt(cerr);
        cerr << setfill('0') << setw(2) << hex << (int)(uint8_t)data[i] << " ";
        if (i % 8 == 7) {
            cerr << endl;
        }
        cerr.copyfmt(state);
    }
    cerr << endl;
#endif
   
    // Read them back
    stream::for_each<message_t>(datastream, check_message);

}

TEST_CASE("Multiple write calls work correctly on the same stream", "[stream]") {
    stringstream datastream;

    // Define some functions to make and check fake Protobuf objects
    using message_t = Position;
    
    size_t index_to_make = 0;
    auto get_message = [&](size_t index) {
        message_t item;
        item.set_node_id(index_to_make);
        index_to_make++;
        return item;
    };
    
    size_t index_expected = 0;
    auto check_message = [&](const message_t& item) {
        REQUIRE(item.node_id() == index_expected);
        index_expected++;
    };
    
    for (size_t i = 0; i < 10; i++) {
        // Serialize some objects
        REQUIRE(stream::write<message_t>(datastream, 1, get_message));
    }
    stream::finish(datastream);
    
    // Read them back
    stream::for_each<message_t>(datastream, check_message);

}

/// Deconstruct a virtual offset into its component parts
static pair<size_t, size_t> unvo(int64_t virtual_offset) {
    pair<size_t, size_t> to_return;
    
    to_return.first = virtual_offset >> 16;
    to_return.second = virtual_offset & 0xFFFF;
    return to_return;
}

TEST_CASE("ProtobufIterator can read serialized data", "[stream]") {
    stringstream datastream;

    // Define some functions to make and check fake Protobuf objects
    using message_t = Position;
    
    // Keep a map so we can look up the group offset for saved items
    unordered_map<size_t, int64_t> index_to_group;
    
    size_t index_to_make = 0;
    auto get_message = [&](size_t index) {
        message_t item;
        item.set_node_id(index_to_make);
        index_to_make++;
        return item;
    };
    
    for (size_t i = 0; i < 10; i++) {
        // Serialize some objects (20, in groups of 2)
        REQUIRE(stream::write<message_t>(datastream, 2, get_message));
    }
    stream::finish(datastream);
    
    {
        // Scan and populate the table
        stream::ProtobufIterator<message_t> it(datastream);
        
        size_t index_found = 0;
        while (it.has_next()) {
#ifdef debug
        cerr << "We wrote " << index_found << " at VO " << it.tell_group() << endl;
#endif
        
            index_to_group[index_found] = it.tell_group();
            index_found++;
            ++it;
        }
        
    }
    
    // Start over
    datastream = stringstream(datastream.str());
    
    SECTION("Data can be found by seeking") {
        stream::ProtobufIterator<message_t> it(datastream);
        
#ifdef debug
        cerr << "Try and load from VO " << index_to_group.at(4) << endl;
#endif
        
        // We know #4 should lead its group.
        bool sought = it.seek_group(index_to_group.at(4));
        REQUIRE(sought == true);
        REQUIRE((*it).node_id() == 4);
    }
    
    SECTION("Data can be iterated back all in a run") {
        size_t index_expected = 0;
        for (stream::ProtobufIterator<message_t> it(datastream); it.has_next(); it.get_next()) {
            auto vo_parts = unvo(it.tell_group());
#ifdef debug
            cerr << "Found item " << (*it).node_id() << " at VO " << it.tell_group()
                << " = " << vo_parts.first << ", " << vo_parts.second << endl;
#endif
        
            // Each item should be the right item
            REQUIRE((*it).node_id() == index_expected);
            // And it should be in the right group at the right place
            //REQUIRE(it.tell_group() == index_to_group.at(index_expected));
            index_expected++;
        }
    }
}

}

}
