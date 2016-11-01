/**
 * unittest/constructor.cpp: test cases for the vg graph constructor tool
 */

#include "catch.hpp"
#include "../constructor.hpp"

#include "../path.hpp"
#include "../json2pb.h"

#include <vector>
#include <sstream>
#include <iostream>

namespace vg {
namespace unittest {

TEST_CASE( "An empty chunk with no variants can be constructed", "[constructor]" ) {
    Constructor constructor;
    
    auto result = constructor.construct_chunk("", "empty", std::vector<vcflib::Variant>());
    
    SECTION("the graph should have no elements") {
        REQUIRE(result.graph.node_size() == 0);
        REQUIRE(result.graph.edge_size() == 0);
        REQUIRE(result.left_ends.empty());
        REQUIRE(result.right_ends.empty());
    }

}

TEST_CASE( "A small linear chunk with no variants can be constructed", "[constructor]" ) {
    Constructor constructor;
    
    auto result = constructor.construct_chunk("GATTACA", "movie", std::vector<vcflib::Variant>());
    
    SECTION("the graph should have one node") {
        REQUIRE(result.graph.node_size() == 1);
        auto& node = result.graph.node(0);
        
        SECTION("the node should have the full sequence") {
            REQUIRE(node.sequence() == "GATTACA");
        }
        
        SECTION("the node should have the correct ID") {
            REQUIRE(node.id() == 1);
        }
        
        SECTION("the node should be the only exposed node on the left") {
            REQUIRE(result.left_ends.count(node.id()));
            REQUIRE(result.left_ends.size() == 1);
        }
        
        SECTION("the node should be the only exposed node on the right") {
            REQUIRE(result.right_ends.count(node.id()));
            REQUIRE(result.right_ends.size() == 1);
        }
    }
    
    SECTION("the graph should have no edges") {
        REQUIRE(result.graph.edge_size() == 0);
    }
    
    SECTION("the graph should have one path") {
        REQUIRE(result.graph.path_size() == 1);
        
        auto& path = result.graph.path(0);
        
        SECTION("the path should have the name we passed in") {
            REQUIRE(path.name() == "movie");
        }
        
        SECTION("the path should have one mapping") {
            REQUIRE(path.mapping_size() == 1);
            
            auto& mapping = path.mapping(0);
            
            SECTION("the mapping should be a full length perfect match") {
                REQUIRE(mapping_is_match(mapping));
                REQUIRE(from_length(mapping) == 7);
            }
            
            SECTION("the mapping should be on the node") {
                REQUIRE(mapping.position().node_id() == 1);
                REQUIRE(mapping.position().offset() == 0);
                REQUIRE(mapping.position().is_reverse() == false);
            }
        }
    }
}

TEST_CASE( "Max node length is respected", "[constructor]" ) {
    Constructor constructor;
    constructor.max_node_size = 4;
    
    auto result = constructor.construct_chunk("GATTACA", "movie", std::vector<vcflib::Variant>());
    
    SECTION("the graph should have two nodes") {
        REQUIRE(result.graph.node_size() == 2);
        auto& node1 = result.graph.node(0);
        auto& node2 = result.graph.node(1);
        
        SECTION("node 1 should have the first part of the sequence") {
            REQUIRE(node1.sequence() == "GATT");
            REQUIRE(node1.id() == 1);
        }
        
        SECTION("node 2 should have the second part of the sequence") {
            REQUIRE(node2.sequence() == "ACA");
            REQUIRE(node2.id() == 2);
        }
        
        SECTION("node 1 should be exposed on the left") {
            REQUIRE(result.left_ends.count(node1.id()));
            REQUIRE(result.left_ends.size() == 1);
        }
        
        SECTION("node 2 should be exposed on the right") {
            REQUIRE(result.right_ends.count(node2.id()));
            REQUIRE(result.right_ends.size() == 1);
        }
    }
    
    SECTION("the graph should have one edge") {
        REQUIRE(result.graph.edge_size() == 1);
        
        auto& edge = result.graph.edge(0);
        
        SECTION("the edge should connect node 1 to node 2") {
            REQUIRE(edge.from() == 1);
            REQUIRE(edge.to() == 2);
            REQUIRE(edge.from_start() == false);
            REQUIRE(edge.to_end() == false);
        }
    }
    
    SECTION("the graph should have one path") {
        REQUIRE(result.graph.path_size() == 1);
        
        auto& path = result.graph.path(0);
        
        SECTION("the path should have the name we passed in") {
            REQUIRE(path.name() == "movie");
        }
        
        SECTION("the path should have two mappings") {
            REQUIRE(path.mapping_size() == 2);
            
            auto& mapping1 = path.mapping(0);
            auto& mapping2 = path.mapping(1);
            
            SECTION("mapping 1 should be a full length perfect match") {
                REQUIRE(mapping_is_match(mapping1));
                REQUIRE(from_length(mapping1) == 4);
            }
            
            SECTION("mapping 2 should be a full length perfect match") {
                REQUIRE(mapping_is_match(mapping2));
                REQUIRE(from_length(mapping2) == 3);
            }
            
            SECTION("mapping 1 should be on node 1") {
                REQUIRE(mapping1.position().node_id() == 1);
                REQUIRE(mapping1.position().offset() == 0);
                REQUIRE(mapping1.position().is_reverse() == false);
            }
            
            SECTION("mapping 2 should be on node 2") {
                REQUIRE(mapping2.position().node_id() == 2);
                REQUIRE(mapping2.position().offset() == 0);
                REQUIRE(mapping2.position().is_reverse() == false);
            }
        }
    }
}

/**
 * Testing wrapper to build a graph from a VCF string.
 */
ConstructedChunk construct_test_chunk(string ref_sequence, string ref_name, string vcf_data) {
    
    // Make a stream out of the data
    std::stringstream vcf_stream(vcf_data);
    
    // Load it up in vcflib
    vcflib::VariantCallFile vcf;
    vcf.open(vcf_stream);
    
    // Fill in this vector of variants
    std::vector<vcflib::Variant> variants;
    vcflib::Variant var;
    while (vcf.getNextVariant(var)) {
        // Make sure to correct each variant's position to 0-based
        var.position -= 1;
        variants.push_back(var);
    }

    Constructor constructor;

    // Construct the graph    
    return constructor.construct_chunk(ref_sequence, ref_name, variants);
}

TEST_CASE( "A SNP can be constructed", "[constructor]" ) {

    // We'll work on this tiny VCF
    auto vcf_data = R"(##fileformat=VCFv4.0
##fileDate=20090805
##source=myImputationProgramV3.1
##reference=1000GenomesPilot-NCBI36
##phasing=partial
##FILTER=<ID=q10,Description="Quality below 10">
##FILTER=<ID=s50,Description="Less than 50% of samples have data">
##FORMAT=<ID=GT,Number=1,Type=String,Description="Genotype">
#CHROM	POS	ID	REF	ALT	QUAL	FILTER	INFO	FORMAT
ref	5	rs1337	A	G	29	PASS	.	GT
)";

    auto ref = "GATTACA";

    // Build the graph
    auto result = construct_test_chunk(ref, "ref", vcf_data);

#ifdef debug
    std::cerr << pb2json(result.graph) << std::endl;
#endif

    // The graph will have these 4 nodes: before and after the SNP, and the two
    // alts of the SNP.
    Node before;
    Node after;
    Node snp_ref;
    Node snp_alt;
    
    // Find all the nodes. All the other test cases depend on knowing them.
    for (size_t i = 0; i < result.graph.node_size(); i++) {
        auto& node = result.graph.node(i);
        
        if (node.sequence() == "GATT") {
            before = node;
        } else if (node.sequence() == "CA") {
            after = node;
        } else if (node.sequence() == "A") {
            snp_ref = node;
        } else if (node.sequence() == "G") {
            snp_alt = node;
        }
    }

    SECTION("the graph should have 4 nodes") {
        REQUIRE(result.graph.node_size() == 4);
        
        SECTION("before, after, ref, and alt nodes should be present") {
            REQUIRE(before.id() != 0);
            REQUIRE(after.id() != 0);
            REQUIRE(snp_ref.id() != 0);
            REQUIRE(snp_alt.id() != 0);
        }
        
    }
    
    SECTION("the graph should have 4 edges") {
        REQUIRE(result.graph.edge_size() == 4);
        
        // We want to recognize all the edges based on the nodes.
        Edge to_ref;
        Edge from_ref;
        Edge to_alt;
        Edge from_alt;
        
        for (size_t i = 0; i < result.graph.edge_size(); i++) {
            auto& edge = result.graph.edge(i);
            
            // Match each edge we expect
            if (edge.from() == before.id() && edge.to() == snp_ref.id()) {
                to_ref = edge;
            } else if (edge.from() == before.id() && edge.to() == snp_alt.id()) {
                to_alt = edge;
            } else if (edge.from() == snp_ref.id() && edge.to() == after.id()) {
                from_ref = edge;
            } else if (edge.from() == snp_alt.id() && edge.to() == after.id()) {
                from_alt = edge;
            }
        }
        
        SECTION("edges should connect into and out of both ref and alt alleles") {
            // Now check them
            REQUIRE(to_ref.from() == before.id());
            REQUIRE(to_ref.to() == snp_ref.id());
            REQUIRE(!to_ref.from_start());
            REQUIRE(!to_ref.to_end());
            
            REQUIRE(to_alt.from() == before.id());
            REQUIRE(to_alt.to() == snp_alt.id());
            REQUIRE(!to_alt.from_start());
            REQUIRE(!to_alt.to_end());
            
            REQUIRE(from_ref.from() == snp_ref.id());
            REQUIRE(from_ref.to() == after.id());
            REQUIRE(!from_ref.from_start());
            REQUIRE(!from_ref.to_end());
            
            REQUIRE(from_alt.from() == snp_alt.id());
            REQUIRE(from_alt.to() == after.id());
            REQUIRE(!from_alt.from_start());
            REQUIRE(!from_alt.to_end());
        }
        
        
    }
    
    SECTION("the graph should have three named paths") {
        REQUIRE(result.graph.path_size() == 3);
        
        // Find the primary path, and the paths for the two alleles
        Path primary;
        Path allele0;
        Path allele1;
        
        for (size_t i = 0; i < result.graph.path_size(); i++) {
            auto& path = result.graph.path(i);
            
            // Path names can't be empty for us to inspect them how we want.
            REQUIRE(path.name().size() > 0);
            
            if (path.name() == "ref") {
                primary = path;
            } else if (path.name()[path.name().size() - 1] == '0') {
                // The name ends with 0, so it ought to be the ref allele path
                allele0 = path;
            } else if (path.name()[path.name().size() - 1] == '1') {
                // The name ends with 1, so it ought to be the alt allele path
                allele1 = path;
            }
        }
        
        SECTION("primary, ref allele, and alt allele paths should be named correctly") {
            REQUIRE(primary.name() == "ref");
            REQUIRE(allele0.name().substr(0, 5) == "_alt_");
            REQUIRE(allele0.name().substr(allele0.name().size() - 2, 2) == "_0");
            REQUIRE(allele1.name().substr(0, 5) == "_alt_");
            REQUIRE(allele1.name().substr(allele1.name().size() - 2, 2) == "_1");
            
            // And the two alleles have to be of the same variant
            REQUIRE(allele0.name().substr(5, allele0.name().size() - (5 + 2)) == 
                allele1.name().substr(5, allele1.name().size() - (5 + 2)));
                
            SECTION("the primary path should trace the reference") {
                REQUIRE(primary.mapping_size() == 3);
                
                REQUIRE(primary.mapping(0).position().node_id() == before.id());
                REQUIRE(primary.mapping(0).position().offset() == 0);
                REQUIRE(primary.mapping(0).position().is_reverse() == false);
                REQUIRE(mapping_is_match(primary.mapping(0)));
                REQUIRE(from_length(primary.mapping(0)) == before.sequence().size());
                
                REQUIRE(primary.mapping(1).position().node_id() == snp_ref.id());
                REQUIRE(primary.mapping(1).position().offset() == 0);
                REQUIRE(primary.mapping(1).position().is_reverse() == false);
                REQUIRE(mapping_is_match(primary.mapping(1)));
                REQUIRE(from_length(primary.mapping(1)) == snp_ref.sequence().size());
                
                REQUIRE(primary.mapping(2).position().node_id() == after.id());
                REQUIRE(primary.mapping(2).position().offset() == 0);
                REQUIRE(primary.mapping(2).position().is_reverse() == false);
                REQUIRE(mapping_is_match(primary.mapping(2)));
                REQUIRE(from_length(primary.mapping(2)) == after.sequence().size());
                
            }
            
            SECTION("the ref allele path should visit the ref allele") {
                REQUIRE(allele0.mapping_size() == 1);
                
                REQUIRE(allele0.mapping(0).position().node_id() == snp_ref.id());
                REQUIRE(allele0.mapping(0).position().offset() == 0);
                REQUIRE(allele0.mapping(0).position().is_reverse() == false);
                REQUIRE(mapping_is_match(allele0.mapping(0)));
                REQUIRE(from_length(allele0.mapping(0)) == snp_ref.sequence().size());
            }
            
            SECTION("the alt allele path should visit the alt allele") {
                REQUIRE(allele1.mapping_size() == 1);
                
                REQUIRE(allele1.mapping(0).position().node_id() == snp_alt.id());
                REQUIRE(allele1.mapping(0).position().offset() == 0);
                REQUIRE(allele1.mapping(0).position().is_reverse() == false);
                REQUIRE(mapping_is_match(allele0.mapping(0)));
                REQUIRE(from_length(allele0.mapping(0)) == snp_alt.sequence().size());
            }
                
        }
    }
	

}

#define debug
TEST_CASE( "A deletion can be constructed", "[constructor]" ) {

    auto vcf_data = R"(##fileformat=VCFv4.0
##fileDate=20090805
##source=myImputationProgramV3.1
##reference=1000GenomesPilot-NCBI36
##phasing=partial
##FILTER=<ID=q10,Description="Quality below 10">
##FILTER=<ID=s50,Description="Less than 50% of samples have data">
##FORMAT=<ID=GT,Number=1,Type=String,Description="Genotype">
#CHROM	POS	ID	REF	ALT	QUAL	FILTER	INFO	FORMAT
ref	5	rs1337	AC	A	29	PASS	.	GT
)";

    auto ref = "GATTACA";

    // Build the graph
    auto result = construct_test_chunk(ref, "ref", vcf_data);
    
#ifdef debug
    std::cerr << pb2json(result.graph) << std::endl;
#endif
    
    // We could build this either GATT,A,C,A or GATTA,C,A.
    // In either case we have as many nodes as edges.
    
    SECTION("the graph should have either 3 or 4 nodes depending on structure") {
        REQUIRE(result.graph.node_size() >= 3);
        REQUIRE(result.graph.node_size() <= 4);
    }
    
    SECTION("the graph should have as many edges as nodes") {
        REQUIRE(result.graph.edge_size() == result.graph.node_size());
    }

    SECTION("the graph should have 3 paths") {
        REQUIRE(result.graph.path_size() == 3);
        
        // Find the primary path, and the paths for the two alleles
        Path primary;
        Path allele0;
        Path allele1;
        
        for (size_t i = 0; i < result.graph.path_size(); i++) {
            auto& path = result.graph.path(i);
            
            // Path names can't be empty for us to inspect them how we want.
            REQUIRE(path.name().size() > 0);
            
            if (path.name() == "ref") {
                primary = path;
            } else if (path.name()[path.name().size() - 1] == '0') {
                // The name ends with 0, so it ought to be the ref allele path
                allele0 = path;
            } else if (path.name()[path.name().size() - 1] == '1') {
                // The name ends with 1, so it ought to be the alt allele path
                allele1 = path;
            }
        }
        
        SECTION("the path for the alt should not include the deleted sequence") {
            if (allele1.mapping_size() == 0) {
                // This definitely lacks the C
                REQUIRE(true);
            } else {
                for (size_t i = 0; i < allele1.mapping_size(); i++) {
                    // Look at all the nodes along the path
                    id_t node_id = allele1.mapping(i).position().node_id();
                    
                    for (size_t j = 0; j < result.graph.node_size(); j++) {
                        // Brute force the whole graph to find the node
                        if(node_id == result.graph.node(j).id()) {
                            // In the node we actually visit, there can't be a "C", since we deleted them all
                            REQUIRE(result.graph.node(j).sequence().find("C") == string::npos);
                        }
                    }
                }
            }
        }
    }

}

TEST_CASE( "A small VCF can be constructed", "[constructor]" ) {

    auto vcf_data = R"(##fileformat=VCFv4.0
##fileDate=20090805
##source=myImputationProgramV3.1
##reference=1000GenomesPilot-NCBI36
##phasing=partial
##FILTER=<ID=q10,Description="Quality below 10">
##FILTER=<ID=s50,Description="Less than 50% of samples have data">
##FORMAT=<ID=GT,Number=1,Type=String,Description="Genotype">
#CHROM	POS	ID	REF	ALT	QUAL	FILTER	INFO	FORMAT
20	14370	rs6054257	G	A	29	PASS	.	GT
20	17330	.	T	A	3	q10	.	GT
20	1110696	rs6040355	A	G,T	67	PASS	.	GT
20	1230237	.	T	.	47	PASS	.	GT
20	1234567	microsat1	GTCT	G,GTACT	50	PASS	.	GT
)";

	

}

// TODO: graphs with variants

}
}
