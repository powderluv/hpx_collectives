//  Copyright (c) 2020 Christopher Taylor
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#ifndef __HPX_GATHER_HPP__
#define __HPX_GATHER_HPP__

#include "collective_traits.hpp" 

namespace hpx { namespace utils { namespace collectives {

template< typename CommunicationPattern, typename BlockingPolicy, typename Serialization >
class gather {
public:
    using communication_pattern = CommunicationPattern;
    using blocking_policy = BlockingPolicy;

    gather(const std::int64_t root_=0);

    template<typename InputIterator, typename OutputIterator>
    void operator()(InputIterator input_beg, InputIterator input_end, OutputIterator out_beg);
};

} /* end namespace collectives */ } /* end namespace utils */ } /* end namespace hpx */

// inspired by https://github.com/pmodels/mpich/blob/master/src/mpi/coll/reduce/reduce_intra_binomial.c

#endif
