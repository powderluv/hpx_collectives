//  Copyright (c) 2020 Christopher Taylor
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#ifndef __HPX_GATHER_BINARY_HPP__
#define __HPX_GATHER_BINARY_HPP__

#include <string>
#include <vector>
#include <sstream>
#include <iterator>
#include <unistd.h>

#include <hpx/include/async.hpp>
#include <hpx/lcos/barrier.hpp>
#include <hpx/lcos/distributed_object.hpp>

#include "collective_traits.hpp" 
#include "gather.hpp"
#include "serialization.hpp"

using hpx::lcos::distributed_object;

REGISTER_DISTRIBUTED_OBJECT_PART(std::tuple<std::int32_t, std::string>);

namespace hpx { namespace utils { namespace collectives {

template<typename BlockingPolicy, typename Serialization >
class gather<tree_binary, BlockingPolicy, Serialization> {

    using value_type_t = typename Serialization::value_type;
    using serializer_t = typename Serialization::serializer;
    using deserializer_t = typename Serialization::deserializer;

private:
    std::int64_t root;
    std::int64_t cas_count;
    std::int64_t rel_rank;
    hpx::distributed_object< std::tuple< std::int32_t, std::int32_t, std::vector<std::string>, std::vector<std::string> > > args;

public:
    using communication_pattern = hpx::utils::collectives::tree_binary;
    using blocking_policy = BlockingPolicy;

    gather(const std::string agas_name, const std::int64_t root_=0) :
        root(root_),
        cas_count(0),
        rel_rank(0),
        args{agas_name, std::make_tuple(0, 0, std::vector<std::string>{}, std::vector<std::string>{})} {

        const auto rank_n = hpx::find_all_localities().size();
        rel_rank = (hpx::get_locality_id()+root_) % rank_n;
        const std::int64_t left = (2*rel_rank) + 1;
        const std::int64_t right = (2*rel_rank) + 2;
        cas_count = ( left < rank_n ) + ( right < rank_n );
    }

    template<typename InputIterator, typename OutputIterator>
    void operator()(InputIterator input_beg, InputIterator input_end, OutputIterator out_beg) {
        // https://en.cppreference.com/w/cpp/header/iterator
        //
        using value_type = typename std::iterator_traits<InputIterator>::value_type;

        auto rank_n = hpx::find_all_localities().size();
        const auto block_size = static_cast<std::int64_t>(input_end - input_beg) /
            static_cast<std::int64_t>(rank_n);

        const std::int64_t logp =
            static_cast<std::int64_t>(
                std::log2(
                    static_cast<double>(
                        rank_n
                    )
                )
            );

        const std::int64_t rank_me = rel_rank;

        // i.am.root.
        if(rank_me == 0) {

            std::vector<std::string> recv_str_vec{};

            if(cas_count > 1) {

                while(!atomic_xchange( &std::get<0>(*args), 1, 0 )) {}
                while(!atomic_xchange( &std::get<1>(*args), 1, 0 )) {}

                recv_str_vec.reserve(std::get<2>(*args).size() + std::get<3>(*args).size());
                recv_str_vec.insert(recv_str_vec.end(), std::get<2>(*args).begin(), std::get<2>(*args).end());
                recv_str_vec.insert(recv_str_vec.end(), std::get<3>(*args).begin(), std::get<3>(*args).end());
            }
            else {
                while(!atomic_xchange( &std::get<0>(*args), 1, 0 )) {}

                recv_str_vec.reserve(std::get<2>(*args).size());
                recv_str_vec.insert(recv_str_vec.end(), std::get<2>(*args).begin(), std::get<2>(*args).end());
            }

            for(auto & recv_str : recv_str_vec) {
                std::int64_t in_rank = 0, in_count = 0;
                value_type_t value_buffer{recv_str};
                deserializer_t iarch{value_buffer};

                iarch >> in_rank >> in_count;
                for(auto count = 0; count < in_count; ++count) {
                    const std::int64_t out_pos = (in_rank*in_count) + count;
                    iarch >> (*(out_beg + out_pos));
                }
            }

        }
        else {

            const std::int64_t parent = (rank_me - 1) / 2;
            const std::int64_t left = (2*rank_me) + 1;
            const std::int64_t right = (2*rank_me) + 2;
            const bool is_leaf = ( left >= rank_n ) && ( right >= rank_n );
            const bool is_even = (rank_me % 2) == 0;

            value_type_t value_buffer{};
            serializer_t value_oa{value_buffer};
            const std::int64_t iter_diff = (input_end-input_beg);

            value_oa << rank_me << iter_diff;
            for(auto seg_itr = input_beg; seg_itr != input_end; ++seg_itr) {
                value_oa << (*seg_itr);
            }

            if(is_leaf) {
                if(is_even) {
                    hpx::async(
                        parent,
                        [](hpx::distributed_object< std::tuple<std::int32_t, std::int32_t, std::vector<std::string>, std::vector<std::string>> > & args_, std::string data_) {
                            std::get<3>(*args_).push_back(data_);
                            atomic_xchange( &std::get<1>(*args_), 0, 1 );
                        }, args, Serialization::get_buffer(value_buffer)
                    );
                }
                else {
                    hpx::async(
                        parent,
                        [](hpx::distributed_object< std::tuple<std::int32_t, std::int32_t, std::vector<std::string>, std::vector<std::string>> > & args_, std::string data_) {
                            std::get<2>(*args_).push_back(data_);
                            atomic_xchange( &std::get<0>(*args_), 0, 1 );
                        }, args, Serialization::get_buffer(value_buffer)
                    );
                }
            }
            else {

                std::vector<std::string> recv_str_vec{};

                if(cas_count > 1) {
                    while(!atomic_xchange( &std::get<0>(*args), 1, 0 )) {}
                    while(!atomic_xchange( &std::get<1>(*args), 1, 0 )) {}

                    recv_str_vec.reserve(std::get<2>(*args).size() + std::get<3>(*args).size());
                    recv_str_vec.insert(recv_str_vec.end(), std::get<2>(*args).begin(), std::get<2>(*args).end());
                    recv_str_vec.insert(recv_str_vec.end(), std::get<3>(*args).begin(), std::get<3>(*args).end());
                }
                else {
                    while(!atomic_xchange( &std::get<0>(*args), 1, 0 )) {}

                    recv_str_vec.reserve(std::get<2>(*args).size());
                    recv_str_vec.insert(recv_str_vec.end(), std::get<2>(*args).begin(), std::get<2>(*args).end());
                }

                recv_str_vec.push_back(value_buffer.str());

                // forced to deserialize/unmarshal data recieved
                // b/c of how boost serialization works; this code
                // repackages the data for transmission
                //
                if(is_even) {
                    hpx::async(
                        parent,
                        [](hpx::distributed_object< std::tuple< std::int32_t, std::int32_t, std::vector<std::string>, std::vector<std::string> > > & args_, std::vector<std::string> data_) {
                            std::get<3>(*args_).reserve(std::get<3>(*args_).size() + data_.size());
                            std::get<3>(*args_).insert(std::get<3>(*args_).end(), data_.begin(), data_.end());
                            atomic_xchange( &std::get<1>(*args_), 0, 1 );
                        }, args, recv_str_vec
                    );
                }
                else {
                    hpx::async(
                        parent,
                        [](hpx::distributed_object< std::tuple< std::int32_t, std::int32_t, std::vector<std::string>, std::vector<std::string> > > & args_, std::vector<std::string> data_) {
                            std::get<2>(*args_).reserve(std::get<2>(*args_).size() + data_.size());
                            std::get<2>(*args_).insert(std::get<2>(*args_).end(), data_.begin(), data_.end());
                            atomic_xchange( &std::get<0>(*args_), 0, 1 );
                        }, args, recv_str_vec
                    );
                }
            } // leaf/non-leaf end
        } // end non-root else

        if constexpr(is_blocking<BlockingPolicy>()) {
            hpx::lcos::barrier b("wait_for_completion", hpx::final_all_localities().size(), hpx::get_locality_id());
            b.wait(); // make sure communications terminate properly
        }

    } // end operator()

};

} /* end namespace collectives */ } /* end namespace utils */ } /* end namespace hpx */

#endif
