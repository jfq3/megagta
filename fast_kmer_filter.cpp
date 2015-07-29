#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <zlib.h>
#include "kseq.h"
#include "prot_kmer_generator.h"
#include "nucl_kmer.h"
#include "hash_set.h"
#include <string.h>
#include <string>
#include <vector>
#include <iostream>
#include "src/sequence/NTSequence.h"
#include "src/sequence/AASequence.h"

//test
#include <bitset>

#ifndef KSEQ_INITED
#define KSEQ_INITED
KSEQ_INIT(gzFile, gzread)
#endif

using namespace std;

struct Sequence {
	string name_;
	string comment_;
	string sequence_;

	Sequence(const string &name, const string &comment, const string &sequence) {
		name_ = name;
		comment_ = comment;
		sequence_ = sequence;
	}
};

struct KmerHelper {
	ProtKmer kmer_;
	string nucl_seq_;
	int frame_;
	int position_;

	KmerHelper() {}

	KmerHelper(const ProtKmer &kmer, const string &nucl_seq, const int &frame, const int &position) {
		kmer_ = kmer;
		nucl_seq_ = nucl_seq;
		frame_ = frame;
		position_ = position;
	}

	uint64_t hash() const {
		NuclKmer dna_kmer = NuclKmer(nucl_seq_);
		return dna_kmer.hash();
		// return kmer_.hash();
	}

	bool operator ==(const KmerHelper &kmer_helper) const {
		if (kmer_.kmers[0] != kmer_helper.kmer_.kmers[0] || kmer_.kmers[1] != kmer_helper.kmer_.kmers[1]) {
			return false;
		}
		return true;
	}
};

void ProcessSequenceMulti(const string &sequence, const string &name, const string &comment, HashSet<ProtKmer> &kmerSet, const int &kmer_size, HashSet<KmerHelper> &starting_kmers);
void ProcessSequence(const string &sequence, const string &name, const string &comment, HashSet<ProtKmer> &kmerSet, const int &kmer_size);
char Comp(char c);
string RevComp(const string &s);


int main(int argc, char **argv) {

	if (argc == 1) {
		fprintf(stderr, "Usage: %s <ref_seq> <read_seq>\n", argv[0]);
		exit(1);
	}

	gzFile fp = gzopen(argv[1], "r");
	gzFile fp2 = gzopen(argv[2], "r");
    kseq_t *seq = kseq_init(fp); // kseq to read files
    kseq_t *seq2 = kseq_init(fp2);

    int kmer_size = 45;
    int batch_size = 100000;

    HashSet<ProtKmer> kmerSet;

    while (kseq_read(seq) >= 0) { 
        // printf("%s\n", seq->seq.s);
        string string_seq(seq->seq.s);
        ProtKmerGenerator kmers = ProtKmerGenerator(string_seq, kmer_size/3, true); // kmer = 45
        while (kmers.hasNext()) {
        	ProtKmer temp = kmers.next();
        	// cout << "seeds = " << temp.decodePacked();
        	std::pair<HashSet<ProtKmer>::iterator, bool> result = kmerSet.insert(temp);
        	// cout << " result: " << result.second << endl;
        }
    }

  //   while (kseq_read(seq2) >= 0) {
  //   	// printf("%s\n", seq2->seq.s); 	
  //   	string string_seq(seq2->seq.s);
  //   	string string_name(seq2->name.s);
  //   	string string_comment;
  //   	if (seq2->comment.l) 
  //   		string_comment = string(seq2->comment.s); 
  //   	else 
  //   		string_comment = "";

  //   	string rc_string_seq = RevComp(string_seq);

  //   	if (string_seq.size() >= kmer_size) {
  //   		ProcessSequence(string_seq, string_name, string_comment, kmerSet, kmer_size);
  //   		ProcessSequence(rc_string_seq, string_name, string_comment, kmerSet, kmer_size);
		// }
  //   }


    	//multi-thread version
    int count = 0;
    vector<Sequence> sequence_storage;
    HashSet<KmerHelper> starting_kmers;
    while (int ret = kseq_read(seq2) >= 0) {
    	sequence_storage.push_back(Sequence(string(seq2->name.s), string(seq2->comment.s), string(seq2->seq.s)));
    	if (++count == batch_size) {
    		count = 0;
    		#pragma omp parallel for
	    	for (int i = 0; i < batch_size; i++) {
	    		string string_seq = sequence_storage[i].sequence_;
	    		string string_name = sequence_storage[i].name_;
	    		string string_comment = sequence_storage[i].comment_;
	    		string rc_string_seq = RevComp(string_seq);
		    	if (string_seq.size() >= kmer_size) {
		    		ProcessSequenceMulti(string_seq, string_name, string_comment, kmerSet, kmer_size, starting_kmers);
		    		ProcessSequenceMulti(rc_string_seq, string_name, string_comment, kmerSet, kmer_size, starting_kmers);
				}
	    	}
	    	sequence_storage.clear();
    	}
    }

    //do the remaining job
    if (count > 0) {
    	#pragma omp parallel for
    	for (int i = 0; i < count; i++) {
	    	string string_seq = sequence_storage[i].sequence_;
	    	string string_name = sequence_storage[i].name_;
	    	string string_comment = sequence_storage[i].comment_;
	    	string rc_string_seq = RevComp(string_seq);
	    	// vector<ProtKmerGenerator> kmer_gens;
		   	if (string_seq.size() >= kmer_size) {
		   		ProcessSequenceMulti(string_seq, string_name, string_comment, kmerSet, kmer_size, starting_kmers);
		   		ProcessSequenceMulti(rc_string_seq, string_name, string_comment, kmerSet, kmer_size, starting_kmers);
			}
	    }
    }



    for (HashSet<KmerHelper>::iterator i = starting_kmers.begin(); i != starting_kmers.end() ; i++) {
    	cout << "rplB\t" << "SRR172903.7702200\t" << "357259128\t";
    	printf("%s\ttrue\t%d\t%s\t%d\n", i->nucl_seq_.c_str(), i->frame_, i->kmer_.decodePacked().c_str(), i->position_);
    }

    kseq_destroy(seq);
    gzclose(fp);
	return 0;
}
void ProcessSequenceMulti(const string &sequence, const string &name, const string &comment, HashSet<ProtKmer> &kmerSet, const int &kmer_size, HashSet<KmerHelper> &starting_kmers) {
	vector<ProtKmerGenerator> kmer_gens;
	for (int i = 0; i < 3; i++) {
	    string seq = sequence.substr(i);
	    seq::NTSequence nts = seq::NTSequence(name, comment, seq);
	    seq::AASequence aa = seq::AASequence::translate(nts.begin(), nts.begin() + (nts.size() / 3) * 3);
	    kmer_gens.push_back(ProtKmerGenerator(aa.asString(), kmer_size/3));
	}
	ProtKmer kmer;
    for (int gen = 0; gen < kmer_gens.size(); gen++) {
    	while (kmer_gens[gen].hasNext()) {
    		kmer = kmer_gens[gen].next();
    		HashSet<ProtKmer>::iterator iter = kmerSet.find(kmer);
    		if (iter != NULL) {
    			// cout << kmer.decodePacked() << endl;
    			int nucl_pos = (kmer_gens[gen].getPosition() - 1) * 3 + gen;
    			starting_kmers.insert(KmerHelper(kmer, sequence.substr(nucl_pos, kmer_size), gen+1, kmer_gens[gen].getPosition()));
    		}
    	}
    }	
}

void ProcessSequence(const string &sequence, const string &name, const string &comment, HashSet<ProtKmer> &kmerSet, const int &kmer_size) {
	vector<ProtKmerGenerator> kmer_gens;
	for (int i = 0; i < 3; i++) {
	    string seq = sequence.substr(i);
	    seq::NTSequence nts = seq::NTSequence(name, comment, seq);
	    seq::AASequence aa = seq::AASequence::translate(nts.begin(), nts.begin() + (nts.size() / 3) * 3);
	    // cout << ">" << string_name << endl;
	    // cout << aa.asString() << endl;

	    kmer_gens.push_back(ProtKmerGenerator(aa.asString(), kmer_size/3));
	}

	ProtKmer kmer;
	for (int gen = 0; gen < kmer_gens.size(); gen++) {
	  	while (kmer_gens[gen].hasNext()) {
	   		kmer = kmer_gens[gen].next();

	   		// cout << kmer.decodePacked() << endl;

	   		HashSet<ProtKmer>::iterator iter = kmerSet.find(kmer);
	   		if (iter != NULL) {
	   			int nucl_pos = (kmer_gens[gen].getPosition() - 1) * 3 + gen;
	   			cout << "rplB\t" << "SRR172903.7702200\t" << "357259128\t";
	   			printf("%s\ttrue\t%d\t%s\t%d\n", sequence.substr(nucl_pos, kmer_size).c_str(), gen+1, kmer.decodePacked().c_str(), kmer_gens[gen].getPosition());
	   		}
	   	}
	}
}

char Comp(char c) {
	switch (c) {
		case 'A':
		case 'a': return 'T';
		case 'C':
		case 'c': return 'G';
		case 'G':
		case 'g': return 'C';
		case 'T':
		case 't': return 'A';
		case 'N':
		case 'n': return 'N';
		default: assert(false);
	}
}

string RevComp(const string &s) {
	string ret;
	for (unsigned i = 0; i < s.length(); ++i) {
		ret.push_back(Comp(s[s.length() - 1 - i]));
	}
	return ret;
}
