/**
 * \file
 * constructor.cpp: contains implementations for vg construction functions.
 */


#include "vcf_buffer.hpp"

namespace vg {

using namespace std;

vcflib::Variant* VcfBuffer::get() {
    if(has_buffer) {
        // We have a variant
        return &buffer;
    } else {
        // No next variant loaded.
        return nullptr;
    }
}

void VcfBuffer::handle_buffer() {
    // The variant in the buffer has been dealt with
    assert(has_buffer);
    has_buffer = false;
}

void VcfBuffer::fill_buffer() {
    if(file != nullptr && file->is_open() && !has_buffer && safe_to_get) {
        // Put a new variant in the buffer if we have a file and the buffer was empty.
        has_buffer = safe_to_get = file->getNextVariant(buffer);
#ifdef debug
        cerr << "Variant in buffer: " << buffer << endl;
#endif
    }
}

bool VcfBuffer::has_tabix() const {
    return file && file->usingTabix;
}

bool VcfBuffer::set_region(const string& contig, int64_t start, int64_t end) {
    if(!has_tabix()) {
        // Nothing to seek in (or no file)
        return false;
    }

    // Discard any variants we had.
    has_buffer = false;
    
    // Remember that we can get the next variant now, in case we had hit the end
    // of the VCF.
    safe_to_get = true;

    if(start != -1 && end != -1) {
        // We have a start and end
        return file->setRegion(contig, start, end);
    } else {
        // Just seek to the whole chromosome
        return file->setRegion(contig);
    }
}

VcfBuffer::VcfBuffer(vcflib::VariantCallFile* file) : file(file) {
    // Our buffer needs to know about the VCF file it is reading from, because
    // it cares about the sample names. If it's not associated properely, we
    // can't getNextVariant into it.
    if (file) {
        // But only do it if we actually have a real file
        buffer.setVariantCallFile(file);
    }
}


WindowedVcfBuffer::WindowedVcfBuffer(vcflib::VariantCallFile* file, size_t window_size): reader(file), window_size(window_size) {
    // Nothing to do!
}

bool WindowedVcfBuffer::has_tabix() const {
    return reader.has_tabix();
}

bool WindowedVcfBuffer::set_region(const string& contig, int64_t start, int64_t end) {
    // Clear buffers
    variants_before.clear();
    variants_after.clear();
    current.reset(nullptr);
    
    return reader.set_region(contig, start, end);
}

bool WindowedVcfBuffer::next() {
    if (current.get() != nullptr) {
        // Put the current variant on the list of previous variants
        variants_before.emplace_back(current.release());
    }
    
    if (!variants_after.empty()) {
        // We already know what the new current variant should be, so grab it
        current.reset(variants_after.front().release());
        variants_after.pop_front();
    } else {
        // We need to read the next variant from the VCF and make that the
        // current variant.
        reader.fill_buffer();
        if (reader.get() == nullptr) {
            // Couldn't find anything. Leave current as nullptr.
            return false;
        } else {
            // Copy what we found into a new Variant we own
            current.reset(new vcflib::Variant(*(reader.get())));
            reader.handle_buffer();
        }
    }
    
    // We will always have a current variant if we get here.
    assert(current.get() != nullptr);
    
    // Now we know we have a current variant. We need to drop variants on the
    // left that are on the wrong sequence or have gotten too far away.
    while (!variants_before.empty() &&
        (current->sequenceName != variants_before.front()->sequenceName ||
        current->zeroBasedPosition() - variants_before.front()->zeroBasedPosition() > window_size)) {
        
        // As long as the leftmost variant exists and is on the wrong contig or
        // too far left
        
        // Delete anything we have cached for it
        cached_genotypes.erase(variants_before.front().get());
        
        // Pop it        
        variants_before.pop_front();
    } 
    
    
    // We also need to load in new variants on the right until what's buffered
    // is too far away, or we run out.
    reader.fill_buffer();
    while (reader.get() != nullptr &&
        reader.get()->sequenceName == current->sequenceName &&
        reader.get()->zeroBasedPosition() - current->zeroBasedPosition() <= window_size) {
        
        // As long as we have a next variant on this contig that's in range,
        // grab a copy.
        variants_after.emplace_back(new vcflib::Variant(*(reader.get())));
        reader.handle_buffer();
        reader.fill_buffer();
    }
    
    // We have a (new) current variant
    return true;
}

tuple<vector<vcflib::Variant*>, vcflib::Variant*, vector<vcflib::Variant*>> WindowedVcfBuffer::get() {
    if (current.get() == nullptr) {
        throw runtime_error("Can't get variant when no current variant is loaded.");
    }
    
    // We have to make vectors of all the pointers.
    vector<vcflib::Variant*> before;
    vector<vcflib::Variant*> after;

    transform(variants_before.begin(), variants_before.end(), back_inserter(before), [](unique_ptr<vcflib::Variant>& v) {
        return v.get();
    });
    transform(variants_after.begin(), variants_after.end(), back_inserter(after), [](unique_ptr<vcflib::Variant>& v) {
        return v.get();
    });
    
    return make_tuple(before, current.get(), after);
}

tuple<vector<vcflib::Variant*>, vcflib::Variant*, vector<vcflib::Variant*>> WindowedVcfBuffer::get_nonoverlapping() {
    if (current.get() == nullptr) {
        throw runtime_error("Can't get variant when no current variant is loaded.");
    }
    
    // We have to make vectors of all the pointers.
    vector<vcflib::Variant*> before;
    vector<vcflib::Variant*> after;
    
    // What's the last base used by a variant? We need this to strip out
    // variants that overlap each other.
    int64_t last_position_used = 0;
    
    for (auto& v : variants_before) {
        // For every variant before the current one
        if (v->zeroBasedPosition() > last_position_used && v->zeroBasedPosition() + v->ref.size() <= current->zeroBasedPosition()) {
            // The end of this variant is before the start of our current variant, so use it.
            before.push_back(v.get());
            // Remember it uses its start base, plus 1 base per ref allele base
            last_position_used = v->zeroBasedPosition() + v->ref.size() - 1;
        }
    }
    
    // Now mark used through the end of the current variant
    last_position_used = current->zeroBasedPosition() + current->ref.size() - 1;
    
    for (auto& v : variants_after) {
        // For every variant after the current one
        if (v->zeroBasedPosition() > last_position_used) {
            // The end of the last variant is before the start of this one, so use it.
            after.push_back(v.get());
            // Remember it uses its start base, plus 1 base per ref allele base
            last_position_used = v->zeroBasedPosition() + v->ref.size() - 1;
        }
    }
    
    return make_tuple(before, current.get(), after);
}

const vector<vector<int>>& WindowedVcfBuffer::get_parsed_genotypes(vcflib::Variant* variant) {

    if (!cached_genotypes.count(variant)) {
        // We need to parse the genotypes for this variant
        
        if (map_order_to_original.empty()) {
            // First we need to build our table converting from sample index in
            // map iteration order to sample index in the original order.
            
            
            // But to do that we need to be able to look up original index by
            // sample name.
            map<string, size_t> original_index_by_name;
            for (size_t i = 0; i < variant->sampleNames.size(); i++) {
                // Basically invert the vector's mapping
                original_index_by_name[variant->sampleNames[i]] = i;
            }
            
            for (auto& kv : variant->samples) {
                // Now we go through the samples (where all the format field
                // data is kept) in map iteration order, and put the actual
                // sample number for each.
                map_order_to_original.push_back(original_index_by_name.at(kv.first));
            }
            
            // We're going to have to account for all the samples in the file.
            assert(map_order_to_original.size() == variant->sampleNames.size());
        }

        // Make a vector to fill in, with one entry per sample
        auto result = cached_genotypes.emplace(piecewise_construct,
            forward_as_tuple(variant), 
            forward_as_tuple(variant->sampleNames.size()));
        assert(result.second);
        auto& genotypes = result.first->second;
        
        // Track what entry we're on in the map from sample to FORMAT values
        size_t map_index = 0;
        for (auto& kv : variant->samples) {
            // Go through all the parsed FORMAT fields for each sample
            
            // Figure out where in the vector by original sample index our
            // result goes.
            size_t original_index = map_order_to_original.at(map_index);
            
            // Pull out the GT value. Explode if there isn't one (though
            // something like "." is acceptable)
            auto& gt_string = kv.second.at("GT").at(0);
            
            // Decompose it and fill in the genotype slot for this sample.
            genotypes[original_index] = decompose_genotype_fast(gt_string);
            
            // Next we'll look at the next sample in map order.
            map_index++;
        }
    }
    return cached_genotypes.at(variant);

}

vector<int> WindowedVcfBuffer::decompose_genotype_fast(const string& genotype) {
    // Rather than doing lots of splits, we just squash atoi in with a single
    // pass over the characters in place.
    
    // We'll fill this in
    vector<int> to_return;
    
    // Guess at size
    to_return.reserve(2);
    
    // We use this for our itoa
    int number = 0;
    for (size_t i = 0; i < genotype.size(); i++) {
        switch(genotype[i]) {
        case '.':
            // We have a missing allele.
            number = vcflib::NULL_ALLELE;
            break;
        case '|':
        case '/':
            // We've terminated a field
            to_return.push_back(number);
            number = 0;
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            // We have a digit, so add it to the growing number
            number *= 10;
            number += (genotype[i] - '0');
            break;
        default:
            throw std::runtime_error("Invalid genotype character in " + genotype);
            break;            
        }
    }
    if(!genotype.empty()) {
        // Finish the last field
        to_return.push_back(number);
    }
    return to_return;
}

}





















