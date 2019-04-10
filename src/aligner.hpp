#ifndef VG_ALIGNER_HPP_INCLUDED
#define VG_ALIGNER_HPP_INCLUDED

#include <algorithm>
#include <utility>
#include <vector>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "gssw.h"
#include "vg.pb.h"
#include "vg.hpp"
#include "Variant.h"
#include "Fasta.h"
#include "path.hpp"
#include "utility.hpp"
#include "banded_global_aligner.hpp"
#include "xdrop_aligner.hpp"
#include "handle.hpp"
#include "backwards_graph.hpp"
#include "null_masking_graph.hpp"
#include "algorithms/distance_to_tail.hpp"

// #define BENCH
// #include "bench.h"

namespace vg {

    static const int8_t default_match = 1;
    static const int8_t default_mismatch = 4;
    static const int8_t default_gap_open = 6;
    static const int8_t default_gap_extension = 1;
    static const int8_t default_full_length_bonus = 5;
    static const int8_t default_max_scaled_score = 32;
    static const uint8_t default_max_qual_score = 255;
    static const double default_gc_content = 0.5;
    static const uint32_t default_xdrop_max_gap_length = 40;

    
    class VG; // forward declaration

    /**
     * The abstract interface that any Aligner should implement.
     */
    class BaseAligner {
    public:
        
        /// Store optimal local alignment against a graph in the Alignment object.
        /// Gives the full length bonus separately on each end of the alignment.
        virtual void align(Alignment& alignment, const HandleGraph& g, bool traceback_aln, bool print_score_matrices) const = 0;
        
        /// Same as previous, but takes advantage of a pre-computed topological order
        virtual void align(Alignment& alignment, const HandleGraph& g, const vector<handle_t>& topological_order,
                           bool traceback_aln, bool print_score_matrices) const = 0;
    };

    /**
     * The basic GSSW-based core aligner implementation, which can then be quality-adjusted or not.
     */
    class GSSWAligner : public BaseAligner {
    protected:
        GSSWAligner() = default;
        ~GSSWAligner();
        
        // for construction
        // needed when constructing an alignable graph from the nodes
        gssw_graph* create_gssw_graph(const HandleGraph& g) const;
        // for constructing an alignable graph when the topological order is already known
        gssw_graph* create_gssw_graph(const HandleGraph& g, const vector<handle_t>& topological_order) const;
        
        // identify the IDs of nodes that should be used as pinning points in GSSW for pinned
        // alignment ((i.e. non-empty nodes as close as possible to sinks))
        unordered_set<id_t> identify_pinning_points(const HandleGraph& graph) const;
        
        // convert graph mapping back into unreversed node positions
        void unreverse_graph_mapping(gssw_graph_mapping* gm) const;
        // convert from graph sequences back into unrereversed form
        void unreverse_graph(gssw_graph* graph) const;
        
        // alignment functions
        void gssw_mapping_to_alignment(gssw_graph* graph,
                                       gssw_graph_mapping* gm,
                                       Alignment& alignment,
                                       bool pinned,
                                       bool pin_left,
                                       bool print_score_matrices = false) const;
        string graph_cigar(gssw_graph_mapping* gm) const;
        
    public:
        /// Given a nonempty vector of nonnegative scaled alignment scores,
        /// compute the mapping quality of the maximal score in the vector.
        /// Sets max_idx_out to the index of that score in the vector. May
        /// modify the input vector.
        static double maximum_mapping_quality_exact(vector<double>& scaled_scores, size_t* max_idx_out); 
        /// Given a nonempty vector of nonnegative scaled alignment scores,
        /// approximate the mapping quality of the maximal score in the vector.
        /// Sets max_idx_out to the index of that score in the vector. May
        /// modify the input vector.
        static double maximum_mapping_quality_approx(vector<double>& scaled_scores, size_t* max_idx_out);
    protected:
        double group_mapping_quality_exact(vector<double>& scaled_scores, vector<size_t>& group) const;
        double estimate_next_best_score(int length, double min_diffs) const;
        
        // must be called before querying mapping_quality
        void init_mapping_quality(double gc_content);
        
        // TODO: this algorithm has numerical problems, just removing it for now
        //vector<double> all_mapping_qualities_exact(vector<double> scaled_scores);
        
    public:

        double max_possible_mapping_quality(int length) const;
        double estimate_max_possible_mapping_quality(int length, double min_diffs, double next_min_diffs) const;
        
        // store optimal alignment against a graph in the Alignment object with one end of the sequence
        // guaranteed to align to a source/sink node
        //
        // pinning left means that that the alignment starts with the first base of the read sequence and
        // the first base of a source node sequence, pinning right means that the alignment starts with
        // the final base of the read sequence and the final base of a sink node sequence
        //
        // Gives the full length bonus only on the non-pinned end of the alignment.
        //
        // assumes that graph is topologically sorted by node index
        virtual void align_pinned(Alignment& alignment, const HandleGraph& g, bool pin_left) const = 0;
        
        /// Same as previous, but takes advantage of a pre-computed topological order
        virtual void align_pinned(Alignment& alignment, const HandleGraph& g,
                                  const vector<handle_t>& topological_order, bool pin_left) const = 0;
        
        // store the top scoring pinned alignments in the vector in descending score order up to a maximum
        // number of alignments (including the optimal one). if there are fewer than the maximum number in
        // the return value, then it includes all alignments with a positive score. the optimal alignment
        // will be stored in both the vector and in the main alignment object
        //
        // assumes that graph is topologically sorted by node index
        virtual void align_pinned_multi(Alignment& alignment, vector<Alignment>& alt_alignments, const HandleGraph& g,
                                        bool pin_left, int32_t max_alt_alns) const = 0;
        
        
        /// Same as previous, but takes advantage of a pre-computed topological order
        virtual void align_pinned_multi(Alignment& alignment, vector<Alignment>& alt_alignments, const HandleGraph& g,
                                        const vector<handle_t>& topological_order, bool pin_left, int32_t max_alt_alns) const = 0;
        
        // store optimal global alignment against a graph within a specified band in the Alignment object
        // permissive banding auto detects the width of band needed so that paths can travel
        // through every node in the graph
        virtual void align_global_banded(Alignment& alignment, const HandleGraph& g,
                                         int32_t band_padding = 0, bool permissive_banding = true) const = 0;
        
        // store top scoring global alignments in the vector in descending score order up to a maximum number
        // of alternate alignments (including the optimal alignment). if there are fewer than the maximum
        // number of alignments in the return value, then the vector contains all possible alignments. the
        // optimal alignment will be stored in both the vector and the original alignment object
        virtual void align_global_banded_multi(Alignment& alignment, vector<Alignment>& alt_alignments,
                                               const HandleGraph& g, int32_t max_alt_alns, int32_t band_padding = 0,
                                               bool permissive_banding = true) const = 0;
        // xdrop aligner
        virtual void align_xdrop(Alignment& alignment, Graph& g, const vector<MaximalExactMatch>& mems, bool reverse_complemented) const = 0;
        virtual void align_xdrop_multi(Alignment& alignment, Graph& g, const vector<MaximalExactMatch>& mems, bool reverse_complemented, int32_t max_alt_alns) const = 0;

        /// Compute the score of an exact match in the given alignment, from the
        /// given offset, of the given length.
        virtual int32_t score_exact_match(const Alignment& aln, size_t read_offset, size_t length) const = 0;
        /// Compute the score of an exact match of the given sequence with the given qualities.
        /// Qualities may be ignored by some implementations.
        virtual int32_t score_exact_match(const string& sequence, const string& base_quality) const = 0;
        /// Compute the score of an exact match of the given range of sequence with the given qualities.
        /// Qualities may be ignored by some implementations.
        virtual int32_t score_exact_match(string::const_iterator seq_begin, string::const_iterator seq_end,
                                          string::const_iterator base_qual_begin) const = 0;
        /// Compute the score of a path against the given range of subsequence with the given qualities.
        virtual int32_t score_partial_alignment(const Alignment& alignment, const HandleGraph& graph, const Path& path,
                                                string::const_iterator seq_begin) const = 0;
        
        /// Returns the score of an insert or deletion of the given length
        int32_t score_gap(size_t gap_length) const;
        
        /// stores -10 * log_10(P_err) in alignment mapping_quality field where P_err is the
        /// probability that the alignment is not the correct one (assuming that one of the alignments
        /// in the vector is correct). alignments must have been created with this Aligner for quality
        /// score to be valid
        void compute_mapping_quality(vector<Alignment>& alignments,
                                     int max_mapping_quality,
                                     bool fast_approximation,
                                     double cluster_mq,
                                     bool use_cluster_mq,
                                     int overlap_count,
                                     double mq_estimate,
                                     double maybe_mq_threshold,
                                     double identity_weight) const;
        /// same function for paired reads, mapping qualities are stored in both alignments in the pair
        void compute_paired_mapping_quality(pair<vector<Alignment>, vector<Alignment>>& alignment_pairs,
                                            const vector<double>& frag_weights,
                                            int max_mapping_quality1,
                                            int max_mapping_quality2,
                                            bool fast_approximation,
                                            double cluster_mq,
                                            bool use_cluster_mq,
                                            int overlap_count1,
                                            int overlap_count2,
                                            double mq_estimate1,
                                            double mq_estimate2,
                                            double maybe_mq_threshold,
                                            double identity_weight) const;
        
        /// Computes mapping quality for the optimal score in a vector of scores
        int32_t compute_mapping_quality(vector<double>& scores, bool fast_approximation) const;
        
        /// Computes mapping quality for a group of scores in a vector of scores (group given by indexes)
        int32_t compute_group_mapping_quality(vector<double>& scores, vector<size_t>& group) const;
        
        /// Returns the  difference between an optimal and second-best alignment scores that would
        /// result in this mapping quality using the fast mapping quality approximation
        double mapping_quality_score_diff(double mapping_quality) const;
        
        /// Convert a score to an unnormalized log likelihood for the sequence.
        /// Requires log_base to have been set.
        double score_to_unnormalized_likelihood_ln(double score) const;
        
        /// The longest gap detectable from a read position without soft-clipping
        size_t longest_detectable_gap(const Alignment& alignment, const string::const_iterator& read_pos) const;
        
        /// The longest gap detectable from a read position without soft-clipping, for a generic read.
        size_t longest_detectable_gap(size_t read_length, size_t read_pos) const;
        
        /// The longest gap detectable from any read position without soft-clipping
        size_t longest_detectable_gap(const Alignment& alignment) const;
        
        /// Use the score values in the aligner to score the given alignment,
        /// scoring gaps caused by jumping between between nodes using a custom
        /// gap length estimation function (which takes the from position, the
        /// to position, and a search limit in bp that happens to be the read
        /// length).
        ///
        /// May include full length bonus or not. TODO: bool flags are bad.
        virtual int32_t score_gappy_alignment(const Alignment& aln,
            const function<size_t(pos_t, pos_t, size_t)>& estimate_distance,
            bool strip_bonuses = false) const;
        
        /// Use the score values in the aligner to score the given alignment assuming
        /// that there are no gaps between Mappings in the Path
        virtual int32_t score_ungapped_alignment(const Alignment& aln,
                                                 bool strip_bonuses = false) const;

        /// Reads a 5x5 substitution scoring matrix from an input stream (can be an ifstream)
        /// expecting 5 whitespace-separated 8-bit integers per line
        virtual void load_scoring_matrix(std::istream& matrix_stream);

        /// Without necessarily rescoring the entire alignment, return the score
        /// of the given alignment with bonuses removed. Assumes that bonuses
        /// are actually included in the score.
        /// Needs to know if the alignment was pinned-end or not, and, if so, which end was pinned.
        virtual int32_t remove_bonuses(const Alignment& aln, bool pinned = false, bool pin_left = false) const;
        
        // members
        int8_t* nt_table = nullptr;
        int8_t* score_matrix = nullptr;
        int8_t match;
        int8_t mismatch;
        int8_t gap_open;
        int8_t gap_extension;
        int8_t full_length_bonus;
        
        // log of the base of the logarithm underlying the log-odds interpretation of the scores
        double log_base = 0.0;
        
    };
    
    /**
     * An ordinary aligner.
     */
    class Aligner : public GSSWAligner {
    private:
        
        // internal function interacting with gssw for pinned and local alignment
        void align_internal(Alignment& alignment, vector<Alignment>* multi_alignments, const HandleGraph& g,
                            const vector<handle_t>* topological_order,
                            bool pinned, bool pin_left, int32_t max_alt_alns,
                            bool traceback_aln,
                            bool print_score_matrices) const;

        // members
        XdropAligner xdrop;
        // bench_t bench;
    public:
        Aligner(int8_t _match = default_match,
                int8_t _mismatch = default_mismatch,
                int8_t _gap_open = default_gap_open,
                int8_t _gap_extension = default_gap_extension,
                int8_t _full_length_bonus = default_full_length_bonus,
                double _gc_content = default_gc_content);
        // xdrop_aligner wrapper, omit default values to call the one above for Aligner();
        Aligner(int8_t _match,
                int8_t _mismatch,
                int8_t _gap_open,
                int8_t _gap_extension,
                int8_t _full_length_bonus,
                double _gc_content,
                uint32_t _xdrop_max_gap_length);
        ~Aligner(void) = default;
        
        /// Store optimal local alignment against a graph in the Alignment object.
        /// Gives the full length bonus separately on each end of the alignment.
        /// Assumes that graph is topologically sorted by node index.
        void align(Alignment& alignment, const HandleGraph& g, bool traceback_aln, bool print_score_matrices) const;
        void align(Alignment& alignment, const HandleGraph& g, const vector<handle_t>& topological_order,
                   bool traceback_aln, bool print_score_matrices) const;
        
        // store optimal alignment against a graph in the Alignment object with one end of the sequence
        // guaranteed to align to a source/sink node
        //
        // pinning left means that that the alignment starts with the first base of the read sequence and
        // the first base of a source node sequence, pinning right means that the alignment starts with
        // the final base of the read sequence and the final base of a sink node sequence
        //
        // Gives the full length bonus only on the non-pinned end of the alignment.
        //
        // assumes that graph is topologically sorted by node index
        void align_pinned(Alignment& alignment, const HandleGraph& g, bool pin_left) const;
        void align_pinned(Alignment& alignment, const HandleGraph& g, const vector<handle_t>& topological_order,
                          bool pin_left) const;
                
        // store the top scoring pinned alignments in the vector in descending score order up to a maximum
        // number of alignments (including the optimal one). if there are fewer than the maximum number in
        // the return value, then it includes all alignments with a positive score. the optimal alignment
        // will be stored in both the vector and in the main alignment object
        //
        // assumes that graph is topologically sorted by node index
        void align_pinned_multi(Alignment& alignment, vector<Alignment>& alt_alignments, const HandleGraph& g,
                                bool pin_left, int32_t max_alt_alns) const;
        void align_pinned_multi(Alignment& alignment, vector<Alignment>& alt_alignments, const HandleGraph& g,
                                const vector<handle_t>& topological_order, bool pin_left, int32_t max_alt_alns) const;
        
        // store optimal global alignment against a graph within a specified band in the Alignment object
        // permissive banding auto detects the width of band needed so that paths can travel
        // through every node in the graph
        void align_global_banded(Alignment& alignment, const HandleGraph& g,
                                 int32_t band_padding = 0, bool permissive_banding = true) const;
        
        // store top scoring global alignments in the vector in descending score order up to a maximum number
        // of alternate alignments (including the optimal alignment). if there are fewer than the maximum
        // number of alignments in the return value, then the vector contains all possible alignments. the
        // optimal alignment will be stored in both the vector and the original alignment object
        void align_global_banded_multi(Alignment& alignment, vector<Alignment>& alt_alignments, const HandleGraph& g,
                                       int32_t max_alt_alns, int32_t band_padding = 0, bool permissive_banding = true) const;

        // xdrop aligner
        void align_xdrop(Alignment& alignment, Graph& g, const vector<MaximalExactMatch>& mems, bool reverse_complemented) const;
        void align_xdrop_multi(Alignment& alignment, Graph& g, const vector<MaximalExactMatch>& mems, bool reverse_complemented, int32_t max_alt_alns) const;

        int32_t score_exact_match(const Alignment& aln, size_t read_offset, size_t length) const;
        int32_t score_exact_match(const string& sequence, const string& base_quality) const;
        int32_t score_exact_match(string::const_iterator seq_begin, string::const_iterator seq_end,
                                  string::const_iterator base_qual_begin) const;
        int32_t score_exact_match(const string& sequence) const;
        int32_t score_exact_match(string::const_iterator seq_begin, string::const_iterator seq_end) const;

        int32_t score_partial_alignment(const Alignment& alignment, const HandleGraph& graph, const Path& path,
                                        string::const_iterator seq_begin) const;
    };

    /**
     * An aligner that uses read base qualities to adjust its scores and alignments.
     */
    class QualAdjAligner : public GSSWAligner {
    public:
        
        QualAdjAligner(int8_t _match = default_match,
                       int8_t _mismatch = default_mismatch,
                       int8_t _gap_open = default_gap_open,
                       int8_t _gap_extension = default_gap_extension,
                       int8_t _full_length_bonus = default_full_length_bonus,
                       int8_t _max_scaled_score = default_max_scaled_score,
                       uint8_t _max_qual_score = default_max_qual_score,
                       double gc_content = default_gc_content);

        ~QualAdjAligner(void) = default;

        // base quality adjusted counterparts to functions of same name from Aligner
        void align(Alignment& alignment, const HandleGraph& g, bool traceback_aln, bool print_score_matrices) const;
        void align(Alignment& alignment, const HandleGraph& g, const vector<handle_t>& topological_order,
                   bool traceback_aln, bool print_score_matrices) const;
        void align_global_banded(Alignment& alignment, const HandleGraph& g,
                                 int32_t band_padding = 0, bool permissive_banding = true) const;
        void align_pinned(Alignment& alignment, const HandleGraph& g, bool pin_left) const;
        void align_pinned(Alignment& alignment, const HandleGraph& g, const vector<handle_t>& topological_order,
                          bool pin_left) const;
        void align_global_banded_multi(Alignment& alignment, vector<Alignment>& alt_alignments, const HandleGraph& g,
                                       int32_t max_alt_alns, int32_t band_padding = 0, bool permissive_banding = true) const;
        void align_pinned_multi(Alignment& alignment, vector<Alignment>& alt_alignments, const HandleGraph& g,
                                bool pin_left, int32_t max_alt_alns) const;
        void align_pinned_multi(Alignment& alignment, vector<Alignment>& alt_alignments, const HandleGraph& g,
                                const vector<handle_t>& topological_order, bool pin_left, int32_t max_alt_alns) const;
        // xdrop aligner
        void align_xdrop(Alignment& alignment, Graph& g, const vector<MaximalExactMatch>& mems, bool reverse_complemented) const;
        void align_xdrop_multi(Alignment& alignment, Graph& g, const vector<MaximalExactMatch>& mems, bool reverse_complemented, int32_t max_alt_alns) const;

        void init_mapping_quality(double gc_content);
        
        int32_t score_exact_match(const Alignment& aln, size_t read_offset, size_t length) const;
        int32_t score_exact_match(const string& sequence, const string& base_quality) const;
        int32_t score_exact_match(string::const_iterator seq_begin, string::const_iterator seq_end,
                                  string::const_iterator base_qual_begin) const;
        
        int32_t score_partial_alignment(const Alignment& alignment, const HandleGraph& graph, const Path& path,
                                        string::const_iterator seq_begin) const;
        
        uint8_t max_qual_score;
        int8_t scale_factor;
        
    private:

        void align_internal(Alignment& alignment, vector<Alignment>* multi_alignments, const HandleGraph& g,
                            const vector<handle_t>* topological_order,
                            bool pinned, bool pin_left, int32_t max_alt_alns,
                            bool traceback_aln,
                            bool print_score_matrices) const;
        

    };
    
    
    /**
     * Holds a set of alignment scores, and has methods to produce aligners of various types on demand, using those scores.
     * Provides a get_aligner() method to get ahold of a useful, possibly quality-adjusted Aligner.
     * Base functionality that is shared between alignment and surjections
     */
    class AlignerClient {
    protected:

        /// Create an AlignerClient, which creates the default aligner instances,
        /// which can depend on a GC content estimate.
        AlignerClient(double gc_content_estimate = vg::default_gc_content);
        
        /// Get the appropriate aligner to use, based on
        /// adjust_alignments_for_base_quality. By setting have_qualities to false,
        /// you can force the non-quality-adjusted aligner, for reads that lack
        /// quality scores.
        const GSSWAligner* get_aligner(bool have_qualities = true) const;
        
        // Sometimes you really do need the two kinds of aligners, to pass to code
        // that expects one or the other.
        const QualAdjAligner* get_qual_adj_aligner() const;
        const Aligner* get_regular_aligner() const;
        
    public:
        /// Set all the aligner scoring parameters and create the stored aligner instances.
        void set_alignment_scores(int8_t match, int8_t mismatch, int8_t gap_open, int8_t gap_extend, int8_t full_length_bonus,
                                  uint32_t xdrop_max_gap_length = default_xdrop_max_gap_length);
                                  
        /// Load a scoring amtrix from a file to set scores
        void load_scoring_matrix(std::ifstream& matrix_stream);
        
        bool adjust_alignments_for_base_quality = false; // use base quality adjusted alignments

    private:
        // GSSW aligners
        unique_ptr<QualAdjAligner> qual_adj_aligner;
        unique_ptr<Aligner> regular_aligner;
        
        // GC content estimate that we need for building the aligners.
        double gc_content_estimate;
    };
    
} // end namespace vg

#endif
