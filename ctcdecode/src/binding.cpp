#include <iostream>
#include <string>
#include <vector>
#include "TH.h"
#include "scorer.h"
#include "ctc_beam_search_decoder.h"
#include "utf8.h"

int utf8_to_utf8_char_vec(const char* labels, std::vector<std::string>& new_vocab) {
    const char* str_i = labels;
    const char* end = str_i + strlen(labels)+1;
    do {
        char u[5] = {0,0,0,0,0};
        uint32_t code = utf8::next(str_i, end);
        if (code == 0) {
            continue;
        }
        utf8::append(code, u);
        new_vocab.push_back(std::string(u));
    }
    while (str_i < end);
}

int beam_decode(THFloatTensor *th_probs,
                const char* labels,
                int vocab_size,
                size_t beam_size,
                size_t num_processes,
                double cutoff_prob,
                size_t cutoff_top_n,
                size_t blank_id,
                void *scorer,
                THIntTensor *th_output,
                THIntTensor *th_timesteps,
                THFloatTensor *th_char_scores,
                THIntTensor *th_scores,
                THIntTensor *th_seq_length)
{
    std::vector<std::string> new_vocab;
    utf8_to_utf8_char_vec(labels, new_vocab);
    Scorer *ext_scorer = NULL;
    if(scorer != NULL){
        ext_scorer = static_cast<Scorer *>(scorer);
    }
    const int64_t max_time = THFloatTensor_size(th_probs, 0);
    const int64_t batch_size = THFloatTensor_size(th_probs, 1);
    const int64_t num_classes = THFloatTensor_size(th_probs, 2);

    // input logits
    std::vector<std::vector<std::vector<double>>> inputs;
    for (int t=0; t < max_time; ++t) {
        std::vector<std::vector<double>> temp (batch_size, std::vector<double>(num_classes));
        for (int b=0; b < batch_size; ++b){
            for (int n=0; n < num_classes; ++n){
                float val = THFloatTensor_get3d(th_probs, t, b, n);
                temp[b][n] = val;
            }
        }
        inputs.push_back(temp);
    }

    std::vector<std::vector<std::pair<double, Output>>> batch_results =
    ctc_beam_search_decoder_batch(inputs, new_vocab, beam_size, num_processes, cutoff_prob, cutoff_top_n, blank_id, ext_scorer);

    for (int b = 0; b < batch_results.size(); ++b){
        std::vector<std::pair<double, Output>> results = batch_results[b];
        for (int p = 0; p < results.size();++p){
            std::pair<double, Output> n_path_result = results[p];
            Output output = n_path_result.second;
            std::vector<int> output_tokens = output.tokens;
            std::vector<int> output_timesteps = output.timesteps;
            std::vector<float> output_scores = output.scores;
            for (int t = 0; t < output_tokens.size(); ++t){
                THIntTensor_set3d(th_output, b, p, t, output_tokens[t]); // fill output tokens
                THIntTensor_set3d(th_timesteps, b, p, t, output_timesteps[t]); // fill timesteps tokens
                THFloatTensor_set3d(th_char_scores, b, p, t, output_scores[t]); // fill score tokens
            }
            THIntTensor_set2d(th_scores, b, p, n_path_result.first); // fill path scores
            THIntTensor_set2d(th_seq_length, b, p, output_tokens.size());
        }
    }
    return 1;
}


extern "C"
{
#include "binding.h"
        int paddle_beam_decode(THFloatTensor *th_probs,
                               const char* labels,
                               int vocab_size,
                               size_t beam_size,
                               size_t num_processes,
                               double cutoff_prob,
                               size_t cutoff_top_n,
                               size_t blank_id,
                               THIntTensor *th_output,
                               THIntTensor *th_timesteps,
                               THFloatTensor *th_char_scores,
                               THIntTensor *th_scores,
                               THIntTensor *th_seq_length){

            return beam_decode(th_probs, labels, vocab_size, beam_size, num_processes,
                        cutoff_prob, cutoff_top_n, blank_id, NULL, th_output, th_timesteps, th_char_scores,
                        th_scores, th_seq_length);
        }

        int paddle_beam_decode_lm(THFloatTensor *th_probs,
                                  const char* labels,
                                  int vocab_size,
                                  size_t beam_size,
                                  size_t num_processes,
                                  double cutoff_prob,
                                  size_t cutoff_top_n,
                                  size_t blank_id,
                                  void *scorer,
                                  THIntTensor *th_output,
                                  THIntTensor *th_timesteps,
                                  THFloatTensor *th_char_scores,
                                  THIntTensor *th_scores,
                                  THIntTensor *th_seq_length){

            return beam_decode(th_probs, labels, vocab_size, beam_size, num_processes,
                        cutoff_prob, cutoff_top_n, blank_id, scorer, th_output, th_timesteps, th_char_scores,
                        th_scores, th_seq_length);
        }


    void* paddle_get_scorer(double alpha,
                            double beta,
                            const char* lm_path,
                            const char* labels,
                            int vocab_size) {
        std::vector<std::string> new_vocab;
        utf8_to_utf8_char_vec(labels, new_vocab);
        Scorer* scorer = new Scorer(alpha, beta, lm_path, new_vocab);
        return static_cast<void*>(scorer);
    }

    int is_character_based(void *scorer){
        Scorer *ext_scorer  = static_cast<Scorer *>(scorer);
        return ext_scorer->is_character_based();
    }
    size_t get_max_order(void *scorer){
        Scorer *ext_scorer  = static_cast<Scorer *>(scorer);
        return ext_scorer->get_max_order();
    }
    size_t get_dict_size(void *scorer){
        Scorer *ext_scorer  = static_cast<Scorer *>(scorer);
        return ext_scorer->get_dict_size();
    }

    void reset_params(void *scorer, double alpha, double beta){
        Scorer *ext_scorer  = static_cast<Scorer *>(scorer);
        ext_scorer->reset_params(alpha, beta);
    }
}
