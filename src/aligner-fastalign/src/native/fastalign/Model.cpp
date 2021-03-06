//
// Created by Davide  Caroselli on 23/08/16.
//

#include "Model.h"
#include "Vocabulary.h"
#include "DiagonalAlignment.h"
#include "Corpus.h"

#include <iostream>
#include <math.h>       /* isnormal */

using namespace std;
using namespace mmt;
using namespace mmt::fastalign;

Model::Model(bool is_reverse, bool use_null, bool favor_diagonal, double prob_align_null, double diagonal_tension)
        : is_reverse(is_reverse), use_null(use_null), favor_diagonal(favor_diagonal), prob_align_null(prob_align_null),
          diagonal_tension(diagonal_tension) {
}

double Model::ComputeAlignments(const vector<pair<wordvec_t, wordvec_t>> &batch, Model *outModel,
                                vector<alignment_t> *outAlignments, const Vocabulary *vocab) {
    double emp_feat = 0.0;

    if (outAlignments)
        outAlignments->resize(batch.size());

#pragma omp parallel for schedule(dynamic) reduction(+:emp_feat)
    for (size_t i = 0; i < batch.size(); ++i) {
        const pair<wordvec_t, wordvec_t> &p = batch[i];
        emp_feat += ComputeAlignment(p.first, p.second, outModel, outAlignments ? &outAlignments->at(i) : NULL, vocab);
    }

    assert(isnormal(emp_feat));
    return emp_feat;
}

double Model::ComputeAlignment(const wordvec_t &source, const wordvec_t &target, Model *outModel,
                               alignment_t *outAlignment, const Vocabulary *vocab) {
    double emp_feat = 0.0;

    const wordvec_t &src = is_reverse ? target : source;
    const wordvec_t &trg = is_reverse ? source : target;

    vector<double> probs(src.size() + 1);

    length_t src_size = (length_t) src.size();
    length_t trg_size = (length_t) trg.size();

    // Geometric mean of grouped data: antilog(sum(f * log x) / N)
    double alg_prob = 0.0;
    double alg_prob_d = 0.0;

    for (length_t j = 0; j < trg_size; ++j) {
        const word_t &f_j = trg[j];
        double sum = 0;
        double prob_a_i = 1.0 / (src_size +
                                 // uniform (model 1), Diagonal Alignment (distortion model)
                                 // ****** DIFFERENT FROM LEXICAL TRANSLATION PROBABILITY *****
                                 (use_null ? 1 : 0));
        if (use_null) {
            if (favor_diagonal)
                prob_a_i = prob_align_null;
            probs[0] = GetProbability(kNullWord, f_j) * prob_a_i;
            sum += probs[0];
        }

        assert(isnormal(sum));
        double az = 0;
        if (favor_diagonal)
            az = DiagonalAlignment::ComputeZ(j + 1, trg_size, src_size, diagonal_tension) /
                 (1. - prob_align_null);

        for (length_t i = 1; i <= src_size; ++i) {
            if (favor_diagonal) {
                prob_a_i = DiagonalAlignment::UnnormalizedProb(j + 1, i, trg_size, src_size, diagonal_tension) /
                           az;
            }
            probs[i] = GetProbability(src[i - 1], f_j) * prob_a_i;
            sum += probs[i];
        }
        assert(isnormal(sum));

        if (use_null) {
            double count = probs[0] / sum;

            assert(isnormal(count));

            if (outModel)
                outModel->IncrementProbability(kNullWord, f_j, count);
        }

        for (length_t i = 1; i <= src_size; ++i) {
            const double p = probs[i] / sum;

            assert(isnormal(p));

            if (outModel)
                outModel->IncrementProbability(src[i - 1], f_j, p);

            emp_feat += DiagonalAlignment::Feature(j, i, trg_size, src_size) * p;
        }


        assert(isnormal(emp_feat));


        if (outAlignment) {
            double max_p = -1;
            int max_index = -1;

            if (use_null) {
                max_index = 0;
                max_p = probs[0];
            }

            for (length_t i = 1; i <= src_size; ++i) {
                if (probs[i] > max_p) {
                    max_index = i;
                    max_p = probs[i];
                }
            }

            score_t word_score = 1;
            if (vocab)
                word_score = vocab->GetProbability(trg[j], is_reverse);

            alg_prob += word_score * log(max_p);
            alg_prob_d += word_score;

            if (max_index > 0) {
                if (is_reverse)
                    outAlignment->points.emplace_back(j, max_index - 1);
                else
                    outAlignment->points.emplace_back(max_index - 1, j);
            }
        }
    }

    if (outAlignment)
        outAlignment->score = (score_t) (alg_prob / alg_prob_d);

    return emp_feat;
}