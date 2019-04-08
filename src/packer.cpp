#include "packer.hpp"
#include "../vg.hpp"

namespace vg {

Packer::Packer(void) : xgidx(nullptr) { }

Packer::Packer(xg::XG* xidx, size_t binsz) : xgidx(xidx), bin_size(binsz) {
    coverage_dynamic = gcsa::CounterArray(xgidx->seq_length, 8);
    if (binsz) n_bins = xgidx->seq_length / bin_size + 1;
}

Packer::~Packer(void) {
    close_edit_tmpfiles();
    remove_edit_tmpfiles();
}

void Packer::load_from_file(const string& file_name) {
    ifstream in(file_name);
    load(in);
}

void Packer::save_to_file(const string& file_name) {
    ofstream out(file_name);
    serialize(out);
}

void Packer::load(istream& in) {
    sdsl::read_member(bin_size, in);
    sdsl::read_member(n_bins, in);
    coverage_civ.load(in);
    edit_csas.resize(n_bins);
    for (size_t i = 0; i < n_bins; ++i) {
        edit_csas[i].load(in);
    }
    // We can only load compacted.
    is_compacted = true;
}

void Packer::merge_from_files(const vector<string>& file_names) {
#ifdef debug
    cerr << "Merging " << file_names.size() << " pack files" << endl;
#endif
    
    // load into our dynamic structures, then compact
    bool first = true;
    for (auto& file_name : file_names) {
        Packer c;
        ifstream f(file_name);
        c.load(f);
        // take bin size and counts from the first, assume they are all the same
        if (first) {
            bin_size = c.get_bin_size();
            n_bins = c.get_n_bins();
            ensure_edit_tmpfiles_open();
            first = false;
        } else {
            assert(bin_size == c.get_bin_size());
            assert(n_bins == c.get_n_bins());
        }
        c.write_edits(tmpfstreams);
        collect_coverage(c);
    }
}

void Packer::merge_from_dynamic(vector<Packer*>& packers) {
    // load dynamic packs into our dynamic structures, then compact
    bool first = true;
    for (auto& p : packers) {
        auto& c = *p;
        c.close_edit_tmpfiles(); // flush and close temporaries
        // take bin size and counts from the first, assume they are all the same
        if (first) {
            bin_size = c.get_bin_size();
            n_bins = c.get_n_bins();
            ensure_edit_tmpfiles_open();
            first = false;
        } else {
            assert(bin_size == c.get_bin_size());
            assert(n_bins == c.get_n_bins());
        }
        c.write_edits(tmpfstreams);
        collect_coverage(c);
    }
}

size_t Packer::get_bin_size(void) const {
    return bin_size;
}

size_t Packer::get_n_bins(void) const {
    return n_bins;
}

size_t Packer::bin_for_position(size_t i) const {
    if (bin_size > 0) {
        return i / bin_size;
    } else {
        return 0;
    }
}

void Packer::write_edits(vector<ofstream*>& out) const {
    for (size_t i = 0; i < n_bins; ++i) {
        write_edits(*out[i], i);
    }
}

void Packer::write_edits(ostream& out, size_t bin) const {
    if (is_compacted) {
        out << extract(edit_csas[bin], 0, edit_csas[bin].size()-2) << delim1; // chomp trailing null, add back delim        
    } else {
        // uncompacted, so just cat the edit file for this bin onto out
        if (edit_tmpfile_names.size()) {
            ifstream edits(edit_tmpfile_names[bin], std::ios_base::binary);
            out << edits.rdbuf() << delim1;
        }
    }
}

void Packer::collect_coverage(const Packer& c) {
    // assume the same basis vector
    assert(!is_compacted);
    for (size_t i = 0; i < c.graph_length(); ++i) {
        coverage_dynamic.increment(i, c.coverage_at_position(i));
    }
}

size_t Packer::serialize(std::ostream& out,
                          sdsl::structure_tree_node* s,
                          std::string name) {
    make_compact();
    sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(s, name, sdsl::util::class_name(*this));
    size_t written = 0;
    written += sdsl::write_member(bin_size, out, child, "bin_size_" + name);
    written += sdsl::write_member(edit_csas.size(), out, child, "n_bins_" + name);
    written += coverage_civ.serialize(out, child, "graph_coverage_" + name);
    for (auto& edit_csa : edit_csas) {
        written += edit_csa.serialize(out, child, "edit_csa_" + name);
    }
    sdsl::structure_tree::add_size(child, written);
    return written;
}

void Packer::make_compact(void) {
    // pack the dynamic countarry and edit coverage into the compact data structure
    if (is_compacted) {
#ifdef debug
        cerr << "Packer is already compact" << endl;
#endif
        return;
    } else {
#ifdef debug
        cerr << "Need to make packer compact" << endl;
#endif
    }
    // sync edit file
    close_edit_tmpfiles();
    // temporaries for construction
    size_t basis_length = coverage_dynamic.size();
    int_vector<> coverage_iv;
    util::assign(coverage_iv, int_vector<>(basis_length));
    for (size_t i = 0; i < coverage_dynamic.size(); ++i) {
        coverage_iv[i] = coverage_dynamic[i];
    }
    edit_csas.resize(edit_tmpfile_names.size());
    util::assign(coverage_civ, coverage_iv);
    construct_config::byte_algo_sa = SE_SAIS;
#pragma omp parallel for
    for (size_t i = 0; i < edit_tmpfile_names.size(); ++i) {
        construct(edit_csas[i], edit_tmpfile_names[i], 1);
    }
    // construct the record marker bitvector
    remove_edit_tmpfiles();
    is_compacted = true;
}

void Packer::make_dynamic(void) {
    if (!is_compacted) return;
    // unpack the compact represenation into the countarray
    assert(false); // not implemented
    is_compacted = false;
}

bool Packer::is_dynamic(void) {
    return !is_compacted;
}

void Packer::ensure_edit_tmpfiles_open(void) {
    if (tmpfstreams.empty()) {
        string base = "vg-pack_";
        string edit_tmpfile_name = temp_file::create(base);
        temp_file::remove(edit_tmpfile_name); // remove this; we'll use it as a base name
        // for as many bins as we have, make a temp file
        tmpfstreams.resize(n_bins);
        edit_tmpfile_names.resize(n_bins);
        for (size_t i = 0; i < n_bins; ++i) {
            edit_tmpfile_names[i] = edit_tmpfile_name+"_"+convert(i);
            tmpfstreams[i] = new ofstream;
            tmpfstreams[i]->open(edit_tmpfile_names[i], std::ios_base::binary);
            assert(tmpfstreams[i]->is_open());
        }
    }
}

void Packer::close_edit_tmpfiles(void) {
    if (!tmpfstreams.empty()) {
        for (auto& tmpfstream : tmpfstreams) {
            *tmpfstream << delim1; // pad
            tmpfstream->close();
            delete tmpfstream;
        }
        tmpfstreams.clear();
    }
}

void Packer::remove_edit_tmpfiles(void) {
    if (!edit_tmpfile_names.empty()) {
        for (auto& name : edit_tmpfile_names) {
            std::remove(name.c_str());
        }
        edit_tmpfile_names.clear();
    }
}

void Packer::add(const Alignment& aln, bool record_edits) {
    // open tmpfile if needed
    ensure_edit_tmpfiles_open();
    // count the nodes, edges, and edits
    for (auto& mapping : aln.path().mapping()) {
        if (!mapping.has_position()) {
#ifdef debug
            cerr << "Mapping has no position" << endl;
#endif
            continue;
        }
        // skip nodes outside of our graph, assuming this may be a subgraph
        if (!xgidx->has_node(mapping.position().node_id())) {
            continue;
        }
        size_t i = position_in_basis(mapping.position());
        for (auto& edit : mapping.edit()) {
            if (edit_is_match(edit)) {
#ifdef debug
                cerr << "Recording a match" << endl;
#endif
                if (mapping.position().is_reverse()) {
                    for (size_t j = 0; j < edit.from_length(); ++j) {
                        coverage_dynamic.increment(i-j);
                    }
                } else {
                    for (size_t j = 0; j < edit.from_length(); ++j) {
                        coverage_dynamic.increment(i+j);
                    }
                }
            } else if (record_edits) {
                // we represent things on the forward strand
                string pos_repr = pos_key(i);
                string edit_repr = edit_value(edit, mapping.position().is_reverse());
                size_t bin = bin_for_position(i);
                *tmpfstreams[bin] << pos_repr << edit_repr;
            }
            if (mapping.position().is_reverse()) {
                i -= edit.from_length();
            } else {
                i += edit.from_length();
            }
        }
    }
}

// find the position on the forward strand in the sequence vector
size_t Packer::position_in_basis(const Position& pos) const {
    // get position on the forward strand
    if (pos.is_reverse()) {
        return (int64_t)xg_node_start(pos.node_id(), xgidx)
            + (int64_t)reverse(pos, xg_node_length(pos.node_id(), xgidx)).offset() - 1;
    } else {
        return (int64_t)xg_node_start(pos.node_id(), xgidx) + (int64_t)pos.offset();
    }
}

string Packer::pos_key(size_t i) const {
    Position pos;
    size_t offset = 2;
    pos.set_node_id(i+offset);
    string pos_repr;
    pos.SerializeToString(&pos_repr);
    stringstream s;
    s << delim1 << delim2 << delim1 << escape_delims(pos_repr);
    return s.str();
}

string Packer::edit_value(const Edit& edit, bool revcomp) const {
    string edit_repr;
    if (revcomp) {
        reverse_complement_edit(edit).SerializeToString(&edit_repr);
    } else {
        edit.SerializeToString(&edit_repr);
    }
    stringstream s;
    s << delim1 << escape_delims(edit_repr);
    return s.str();
}

string Packer::escape_delims(const string& s) const {
    return escape_delim(escape_delim(s, delim1), delim2);
}

string Packer::unescape_delims(const string& s) const {
    return unescape_delim(unescape_delim(s, delim1), delim2);
}

string Packer::escape_delim(const string& s, char d) const {
    string escaped; escaped.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        escaped.push_back(c);
        if (c == d) escaped.push_back(c);
    }
    return escaped;
}

string Packer::unescape_delim(const string& s, char d) const {
    string unescaped; unescaped.reserve(s.size());
    for (size_t i = 0; i < s.size()-1; ++i) {
        char c = s[i];
        char b = s[i+1];
        if (c == d && b == d) {
            unescaped.push_back(c);
        } else {
            unescaped.push_back(c);
            if (i == s.size()-2) unescaped.push_back(b);
        }
    }
    return unescaped;
}

size_t Packer::graph_length(void) const {
    if (is_compacted) {
        return coverage_civ.size();
    } else {
        return coverage_dynamic.size();
    }
}

size_t Packer::coverage_at_position(size_t i) const {
    if (is_compacted) {
        return coverage_civ[i];
    } else {
        return coverage_dynamic[i];
    }
}

vector<Edit> Packer::edits_at_position(size_t i) const {
    vector<Edit> edits;
    if (i == 0) return edits;
    string key = pos_key(i);
    size_t bin = bin_for_position(i);
    auto& edit_csa = edit_csas[bin];
    auto occs = locate(edit_csa, key);
    for (size_t i = 0; i < occs.size(); ++i) {
        // walk from after the key and delim1 to the next end-sep
        size_t b = occs[i] + key.size() + 1;
        size_t e = b;
        // look for an odd number of delims
        // run until we find a delim
        while (true) {
            while (extract(edit_csa, e, e)[0] != delim1) ++e;
            // now we are matching the delim... count them
            size_t f = e;
            while (extract(edit_csa, f, f)[0] == delim1) ++f;
            size_t c = f - e;
            e = f; // set pointer to last delim
            if (c % 2 != 0) {
                break;
            }
        }
        string value = unescape_delims(extract(edit_csa, b, e));
        Edit edit;
        edit.ParseFromString(value);
        edits.push_back(edit);
    }
    return edits;
}

ostream& Packer::as_table(ostream& out, bool show_edits, vector<vg::id_t> node_ids) {
#ifdef debug
    cerr << "Packer table of " << coverage_civ.size() << " rows:" << 
        l;
#endif

    out << "seq.pos" << "\t"
        << "node.id" << "\t"
        << "node.offset" << "\t"
        << "coverage";
    if (show_edits) out << "\t" << "edits";
    out << endl;
    // write the coverage as a vector
    for (size_t i = 0; i < coverage_civ.size(); ++i) {
        id_t node_id = xgidx->node_at_seq_pos(i+1);
        if (!node_ids.empty() && find(node_ids.begin(), node_ids.end(), node_id) == node_ids.end()) {
            continue;
        }
        size_t offset = i - xgidx->node_start(node_id);
        out << i << "\t" << node_id << "\t" << offset << "\t" << coverage_civ[i];
        if (show_edits) {
            out << "\t" << count(edit_csas[bin_for_position(i)], pos_key(i));
            for (auto& edit : edits_at_position(i)) out << " " << pb2json(edit);
        }
        out << endl;
    }
    return out;
}

ostream& Packer::show_structure(ostream& out) {
    out << coverage_civ << endl; // graph coverage (compacted coverage_dynamic)
    for (auto& edit_csa : edit_csas) {
        out << edit_csa << endl;
    }
    //out << " i SA ISA PSI LF BWT    T[SA[i]..SA[i]-1]" << endl;
    //csXprintf(cout, "%2I %2S %3s %3P %2p %3B   %:3T", edit_csa);
    return out;
}

size_t Packer::coverage_size(void) {
    return coverage_civ.size();
}

}
