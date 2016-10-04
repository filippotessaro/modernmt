//
// Created by Davide  Caroselli on 27/09/16.
//

#include <algorithm>
#include <suffixarray/SuffixArray.h>

#include "PhraseTable.h"
#include "UpdateManager.h"

using namespace mmt;
using namespace mmt::sapt;

struct PhraseTable::pt_private {
    SuffixArray *index;
    UpdateManager *updates;
};

PhraseTable::PhraseTable(const string &modelPath, const Options &options) {
    self = new pt_private();
    self->index = new SuffixArray(modelPath, options.prefix_length);
    self->updates = new UpdateManager(self->index, options.update_buffer_size, options.update_max_delay);
    numScoreComponent = 4;
}

PhraseTable::~PhraseTable() {
    delete self->updates;
    delete self->index;
    delete self;
}

void PhraseTable::Add(const updateid_t &id, const domain_t domain, const std::vector<wid_t> &source,
                      const std::vector<wid_t> &target, const alignment_t &alignment) {
    self->updates->Add(id, domain, source, target, alignment);
}

vector<updateid_t> PhraseTable::GetLatestUpdatesIdentifier() {
    const vector<seqid_t> &streams = self->index->GetStreams();

    vector<updateid_t> result;
    result.reserve(streams.size());

    for (size_t i = 0; i < streams.size(); ++i) {
        if (streams[i] != 0)
            result.push_back(updateid_t((stream_t) i, streams[i]));

    }

    return result;
}

void *PhraseTable::__GetSuffixArray() {
    return self->index;
}
void PhraseTable::GetTargetPhraseCollection(const vector<wid_t> &sourcePhrase, size_t limit, vector<TranslationOption> &optionsVec, context_t *context) {

    OptionsMap_t optionsMap;

    vector<sample_t> samples;
    self->index->GetRandomSamples(sourcePhrase, limit, samples, context);

    cout << "PhraseTable::GetTargetPhraseCollection(...) Found " << samples.size() << " samples" << endl;

    for (auto sample=samples.begin(); sample != samples.end(); ++ sample){
        //sample->Print();
        //std::cout << "sample->source.size():" << sample->source.size() << " sample->target.size():" << sample->target.size()<< std::endl;
        //GetTranslationOptions(sourcePhrase, sample->source, sample->target, sample->alignment, sample->offsets, optionsVec);
        GetTranslationOptions(sourcePhrase, sample->source, sample->target, sample->alignment, sample->offsets, optionsMap);
    }
    //std::cerr << "PhraseTable::GetTargetPhraseCollection(...) Found " << optionsVec.size()  << " options" << std::endl;
    //std::cerr << "PhraseTable::GetTargetPhraseCollection(...) Found and Scoring " << optionsMap.size()  << " options" << std::endl;

    //loop over all Options and score them
    ScoreTranslationOptions(optionsMap, samples.size());

    //transform the map into a vector
    for (auto entry = optionsMap.begin(); entry != optionsMap.end(); ++entry) {
        //entry->first.Print();
        optionsVec.push_back(entry->first);
    }
};

void PhraseTable::ScoreTranslationOptions(OptionsMap_t &optionsMap, size_t NumberOfSamples){
    size_t GlobalSourceFrequency = 1;
    size_t GlobalTargetFrequency = 1;
    size_t SampleSourceFrequency = 0;
    for (auto entry = optionsMap.begin(); entry != optionsMap.end(); ++ entry) {
        SampleSourceFrequency += entry->second;
    }
    for (auto entry = optionsMap.begin(); entry != optionsMap.end(); ++ entry) {

        std::vector<float> scores(numScoreComponent);

        //set the forward and backward frequency-based scores of the current option
        scores[0] = (float) entry->second / SampleSourceFrequency;
        scores[1] = ((float) entry->second / GlobalTargetFrequency) * ((float) GlobalSourceFrequency / NumberOfSamples);

        //set the forward and backward lexical scores of the current option
        scores[2] = 0.0;
        scores[3] = 0.0;
        for (auto a = entry->first.alignment.begin(); a != entry->first.alignment.end(); ++a) {
            scores[2] += GetForwardLexicalScore(a->first, a->second);
            scores[3] += GetBackwardLexicalScore(a->first, a->second);
        }
        ((TranslationOption*) &entry->first)->SetScores(scores);
    }
}

float PhraseTable::GetForwardLexicalScore(length_t sourceWord, length_t targetWord){
    return 0.1;
}

float PhraseTable::GetBackwardLexicalScore(length_t sourceWord, length_t targetWord){
    return 0.2;
}


void PhraseTable::GetTranslationOptions(const vector<wid_t> &sourcePhrase,
                                        const std::vector<wid_t> &sourceSentence,
                                        const std::vector<wid_t> &targetSentence,
                                        const alignment_t &alignment,
                                        const std::vector<length_t> &offsets,
                                        OptionsMap_t &optionsMap){

//void PhraseTable::GetTranslationOptions(const vector<wid_t> &sourcePhrase, sample_t &sample, vector<TranslationOption> &outOptions) {

// Keeps a vector to know whether a target word is aligned.
    std::vector<bool> targetAligned(targetSentence.size(),false);


    /*   alignment_t corrected_alignment;
    for (auto alignPoint = alignment.begin(); alignPoint != alignment.end(); ++alignPoint) {
        if ((alignPoint->first < sourceSentence.size()) && (alignPoint->second < targetSentence.size())) {
            corrected_alignment.push_back(*alignPoint);
        }
    }
    */
    //for (auto alignPoint = corrected_alignment.begin(); alignPoint != corrected_alignment.end(); ++alignPoint){
    for (auto alignPoint = alignment.begin(); alignPoint != alignment.end(); ++alignPoint){
        if ((alignPoint->first < sourceSentence.size()) && (alignPoint->second < targetSentence.size())) {
            targetAligned[alignPoint->second] = true;
        }
    }

    for (auto offset = offsets.begin(); offset != offsets.end(); ++ offset){ //for each occurrence of the source in the sampled sentence pair
            //get source position lowerBound  and  upperBound
        int sourceStart = *offset; // lowerBound is always larger than or equal to 0
        int sourceEnd = sourceStart + sourcePhrase.size() - 1; // upperBound is always larger than or equal to 0, because sourcePhrase.size()>=1

        // find the minimally matching foreign phrase
        int targetStart = targetSentence.size() - 1;
        int targetEnd = -1;

        //for (auto alignPoint = corrected_alignment.begin(); alignPoint != corrected_alignment.end(); ++alignPoint) {
        for (auto alignPoint = alignment.begin(); alignPoint != alignment.end(); ++alignPoint) {
            if ((alignPoint->first < sourceSentence.size()) && (alignPoint->second < targetSentence.size())) {
                if ((alignPoint->first >= sourceStart) && (alignPoint->first <= sourceEnd)) {
                    targetStart = std::min((int) alignPoint->second, targetStart);
                    targetEnd = std::max((int) alignPoint->second, targetEnd);
                }
            }
        }

        alignment_t considered_alignment;
        //for (auto alignPoint = corrected_alignment.begin(); alignPoint != corrected_alignment.end(); ++alignPoint) {
        for (auto alignPoint = alignment.begin(); alignPoint != alignment.end(); ++alignPoint) {
            //std::cerr << "\n\nalignPoint->first:" << alignPoint->first << " alignPoint->second:" << alignPoint->second << std::endl;

            if ((alignPoint->first < sourceSentence.size()) && (alignPoint->second < targetSentence.size())) {
                if ( ((alignPoint->first >= sourceStart) && (alignPoint->first <= sourceEnd)) || ((alignPoint->second >= targetStart) && (alignPoint->second <= targetEnd)) ){
                    considered_alignment.push_back(*alignPoint);
                }
            }
        }

/*
        std::cout <<"Considered " << considered_alignment.size() << " points ";
        for (auto point = considered_alignment.begin(); point != considered_alignment.end(); ++ point) {
            std::cout << " " << point->first << "," << point->second;
        }
        std::cout << std::endl;
*/


        //std::cerr << "calling ExtractPhrasePairs for range [" << sourceStart << "," << sourceEnd << "][" << targetStart << "," << targetEnd << "]" << std::endl;

        //ExtractPhrasePairs(sourceSentence, targetSentence, corrected_alignment, targetAligned, sourceStart, sourceEnd, targetStart, targetEnd, outOptions); //add all extracted phrase pairs into outOptions
        //ExtractPhrasePairs(sourceSentence, targetSentence, corrected_alignment, targetAligned, sourceStart, sourceEnd, targetStart, targetEnd, optionsMap); //add all extracted phrase pairs into outOptions
        ExtractPhrasePairs(sourceSentence, targetSentence, considered_alignment, targetAligned, sourceStart, sourceEnd, targetStart, targetEnd, optionsMap); //add all extracted phrase pairs into outOptions
    }
}

void PhraseTable::GetTranslationOptions(const vector<wid_t> &sourcePhrase,
                                        const std::vector<wid_t> &sourceSentence,
                                        const std::vector<wid_t> &targetSentence,
                                        const alignment_t &alignment,
                                        const std::vector<length_t> &offsets,
                                        OptionsVec_t &optionsVec){

//void PhraseTable::GetTranslationOptions(const vector<wid_t> &sourcePhrase, sample_t &sample, vector<TranslationOption> &outOptions) {

// Keeps a vector to know whether a target word is aligned.
    std::vector<bool> targetAligned(targetSentence.size(),false);

    alignment_t corrected_alignment;
    for (auto alignPoint = alignment.begin(); alignPoint != alignment.end(); ++alignPoint) {
        if ( (alignPoint->first < sourceSentence.size()) && (alignPoint->second < targetSentence.size())) {
            corrected_alignment.push_back(*alignPoint);
        }
    }
    for (auto alignPoint = corrected_alignment.begin(); alignPoint != corrected_alignment.end(); ++alignPoint){
        targetAligned[alignPoint->second] = true;
    }

    std::cerr << "targetAligned.size:" << targetAligned.size()  << std::endl;

    for (auto offset = offsets.begin(); offset != offsets.end(); ++ offset){ //for each occurrence of the source in the sampled sentence pair
        std::cerr << "offset:" << *offset  << std::endl;

        //get source position lowerBound  and  upperBound
        length_t sourceStart = *offset; // lowerBound is always larger than or equal to 0
        length_t sourceEnd = sourceStart + sourcePhrase.size() - 1; // upperBound is always larger than or equal to 0, because sourcePhrase.size()>=1

        // find the minimally matching foreign phrase
        int targetStart = targetSentence.size() - 1;
        int targetEnd = -1;

        for (auto alignPoint = corrected_alignment.begin(); alignPoint != corrected_alignment.end(); ++alignPoint) {
//            std::cerr << "\n\nalignPoint->first:" << alignPoint->first << " alignPoint->second:" << alignPoint->second << std::endl;

            if ( (alignPoint->first >= sourceStart ) && (alignPoint->first <= sourceEnd ) ) {
                targetStart = std::min((int) alignPoint->second, targetStart);
                targetEnd = std::max((int) alignPoint->second, targetEnd);
            }

        }

        std::cerr << "calling ExtractPhrasePairs with " << std::endl;
        std::cerr << "sourceStart:" << sourceStart << " sourceEnd:" << sourceEnd << " targetStart:" << targetStart << " targetEnd:" << targetEnd << std::endl;

        ExtractPhrasePairs(sourceSentence, targetSentence, corrected_alignment, targetAligned, sourceStart, sourceEnd, targetStart, targetEnd, optionsVec); //add all extracted phrase pairs into outOptions

    }
}


void PhraseTable::ExtractPhrasePairs(const std::vector<wid_t> &sourceSentence,
                                     const std::vector<wid_t> &targetSentence,
                                     const alignment_t &alignment,
                                     const std::vector<bool> &targetAligned,
                                     int sourceStart, int sourceEnd, int targetStart, int targetEnd,
                                     OptionsMap_t &optionsMap) {

    if (targetEnd < 0) // 0-based indexing.
        return;

// Check if alignment points are consistent. if yes, copy
    alignment_t currentAlignments;
    for (auto alignPoint = alignment.begin(); alignPoint != alignment.end(); ++alignPoint) {

        //std::cout << "PhraseTable::ExtractPhrasePairs(...)  checking " << alignPoint->first << "," << alignPoint->second << " versus range [" << sourceStart << "," << sourceEnd << "][" << targetStart << "," << targetEnd << "]";

        //checking whether there are other alignment points outside the current phrase pair; if yes, return doing nothing, because the phrase pair is not valid
        if ( ( (alignPoint->first >= sourceStart) && (alignPoint->first <= sourceEnd) ) &&  ( (alignPoint->second < targetStart) || (alignPoint->second > targetEnd) ) ){
            //std::cout << " INVALID" << std::endl;;
            return;
        }
        if ( ( (alignPoint->second >= targetStart) && (alignPoint->second <= targetEnd) ) && ( (alignPoint->first < sourceStart) || (alignPoint->first > sourceEnd) ) ){
            //std::cout << " INVALID" << std::endl;;
            return;
        }

        alignmentPoint_t currentPoint(alignPoint->first - sourceStart, alignPoint->second - targetStart);
        currentAlignments.push_back(currentPoint);
    }
    /*
    std::cout << "Selected " << currentAlignments.size() << " points:";
    for (auto alignPoint = currentAlignments.begin(); alignPoint != currentAlignments.end(); ++alignPoint) {
        std::cout << " " << alignPoint->first << "," << alignPoint->second;
    }
    std::cout << std::endl;
    */

    int ts = targetStart;
    while (true) {
        int te = targetEnd;
        while (true) {
            //std::cout << " current from:" << ts << " to " << te << " targetSentence.size():" << targetSentence.size() << std::endl;
// add phrase pair ([e_start, e_end], [fs, fe]) to set E
            TranslationOption option(numScoreComponent);
            //std::cout << "targetPhrase will be from:" << ts << " to " << te << std::endl;
            option.targetPhrase.insert(option.targetPhrase.begin(),
                                       targetSentence.begin() + ts,
                                       targetSentence.begin() + te + 1);
            option.alignment = currentAlignments;

            auto key = optionsMap.find(option);
            if (key != optionsMap.end()) {
                key->second += 1;
            } else {
                optionsMap.insert(std::make_pair(option,1));
            }
            //std::cerr << "PhraseTable::ExtractPhrasePairs(...) Found " << optionsMap.size()  << " options" << std::endl;

            te += 1;
// if fe is in word alignment or out-of-bounds
            if (targetAligned[te] || te == targetSentence.size()) {
                break;
            }
        }
        ts -= 1;
// if fs is in word alignment or out-of-bounds
        if (targetAligned[ts] || ts < 0) {
            break;
        }
    }
}

void PhraseTable::ExtractPhrasePairs(const std::vector<wid_t> &sourceSentence,
                                     const std::vector<wid_t> &targetSentence,
                                     const alignment_t &alignment,
                                     const std::vector<bool> &targetAligned,
                                     int sourceStart, int sourceEnd, int targetStart, int targetEnd,
                                     OptionsVec_t &optionsVec) {

    if (targetEnd < 0) // 0-based indexing.
        return;

// Check if alignment points are consistent. if yes, copy
    alignment_t currentAlignments;
    for (auto alignPoint = alignment.begin(); alignPoint != alignment.end(); ++alignPoint) {

        //std::cout << "checking alignPoint->first:" << alignPoint->first << " alignPoint->second:" << alignPoint->second << " versus sourceStart:" << sourceStart << " sourceEnd:" << sourceEnd << " targetStart:" << targetStart << " targetEnd:" << targetEnd << std::endl;

        if ((alignPoint->first >= sourceStart) && (alignPoint->first <= sourceEnd)) {
            std::cout << "adding alignPoint->first:" << alignPoint->first << " alignPoint->second:" << alignPoint->second << std::endl;
            alignmentPoint_t currentPoint(alignPoint->first,alignPoint->second);
            currentAlignments.push_back(currentPoint);
            std::cout << "now (possible) currentAlignments.size():" << currentAlignments.size() << std::endl;


            if ((alignPoint->second < targetStart) && (alignPoint->second > targetEnd)) {
                std::cout << "alignmentPoint not valid; hence, phrase pair is not legal" << std::endl;
                return;
            }
        }
    }
    std::cout << "Final currentAlignments.size():" << currentAlignments.size() << std::endl;

    int ts = targetStart;
    while (true) {
        int te = targetEnd;
        while (true) {
            std::cout << " current from:" << ts << " to " << te << " targetSentence.size():" << targetSentence.size()<< std::endl;
// add phrase pair ([e_start, e_end], [fs, fe]) to set E
            TranslationOption option(numScoreComponent);
            std::cout << "targetPhrase will be from:" << ts << " to " << te << std::endl;
            option.targetPhrase.insert(option.targetPhrase.begin(), targetSentence.begin() + ts,
                                       targetSentence.begin() + te + 1);
            option.alignment = currentAlignments;

            optionsVec.push_back(option);
            te += 1;
// if fe is in word alignment or out-of-bounds
            if (targetAligned[te] || te == targetSentence.size()) {
                break;
            }
        }
        ts -= 1;
// if fs is in word alignment or out-of-bounds
        if (targetAligned[ts] || ts < 0) {
            break;
        }
    }
}

void PhraseTable::NormalizeContext(context_t *context) {
    context_t ret;
    float total = 0.0;

    for (auto it = context->begin(); it != context->end(); ++it) {
        //todo:: can it happen that the domain is empty?
        total += it->score;
    }

    if (total == 0.0)
        total = 1.0f;

    for (auto it = context->begin(); it != context->end(); ++it) {
        //todo:: can it happen that the domain is empty?
        it->score /= total;

        ret.push_back(*it);
    }

    // replace new vector into old vector
    context->clear();
    //todo: check if the following insert is correct
    context->insert(context->begin(), ret.begin(), ret.end());
}