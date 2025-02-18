/*
 *  inhomogeneous_poisson_generator.cpp
 *
 *  This file is part of NEST.
 *
 *  Copyright (C) 2004 The NEST Initiative
 *
 *  NEST is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NEST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NEST.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "inhomogeneous_poisson_generator.h"

// C++ includes:
#include <cmath>
#include <limits>

// Includes from libnestutil:
#include "dict_util.h"
#include "numerics.h"

// Includes from nestkernel:
#include "event_delivery_manager_impl.h"
#include "exceptions.h"
#include "kernel_manager.h"
#include "universal_data_logger_impl.h"

// Includes from sli:
#include "arraydatum.h"
#include "booldatum.h"
#include "dict.h"
#include "dictutils.h"
#include "doubledatum.h"
#include "integerdatum.h"


/* ----------------------------------------------------------------
 * Default constructors defining default parameter
 * ---------------------------------------------------------------- */

nest::inhomogeneous_poisson_generator::Parameters_::Parameters_()
  : rate_times_()  // ms
  , rate_values_() // spikes/ms,
  , allow_offgrid_times_( false )

{
}

/* ----------------------------------------------------------------
 * Parameter extraction and manipulation functions
 * ---------------------------------------------------------------- */

void
nest::inhomogeneous_poisson_generator::Parameters_::get( DictionaryDatum& d ) const
{
  const size_t n_rates = rate_times_.size();
  std::vector< double >* times_ms = new std::vector< double >();
  times_ms->reserve( n_rates );
  for ( size_t n = 0; n < n_rates; ++n )
  {
    times_ms->push_back( rate_times_[ n ].get_ms() );
  }

  ( *d )[ names::rate_times ] = DoubleVectorDatum( times_ms );
  ( *d )[ names::rate_values ] = DoubleVectorDatum( new std::vector< double >( rate_values_ ) );
  ( *d )[ names::allow_offgrid_times ] = BoolDatum( allow_offgrid_times_ );
}

void
nest::inhomogeneous_poisson_generator::Parameters_::assert_valid_rate_time_and_insert( const double t )
{
  Time t_rate;

  if ( t <= kernel().simulation_manager.get_time().get_ms() )
  {
    throw BadProperty( "Time points must lie strictly in the future." );
  }

  // we need to force the rate time to the grid;
  // first, convert the rate time to tics, may not be on grid
  t_rate = Time::ms( t );
  if ( not t_rate.is_grid_time() )
  {
    if ( allow_offgrid_times_ )
    {
      // in this case, we need to round to the end of the step
      // in which t lies, ms_stamp does that for us.
      t_rate = Time::ms_stamp( t );
    }
    else
    {
      std::stringstream msg;
      msg << "inhomogeneous_poisson_generator: Time point " << t << " is not representable in current resolution.";
      throw BadProperty( msg.str() );
    }
  }

  assert( t_rate.is_grid_time() );

  // t_rate is now the correct time stamp given the chosen options
  // when we get here, we know that the rate time is valid
  rate_times_.push_back( t_rate );
}

void
nest::inhomogeneous_poisson_generator::Parameters_::set( const DictionaryDatum& d, Buffers_& b, Node* )
{
  const bool times = d->known( names::rate_times );
  const bool rates = updateValue< std::vector< double > >( d, names::rate_values, rate_values_ );

  // if offgrid flag changes, it must be done so either before any rates are
  // set or when setting new rates (which removes old ones)
  if ( d->known( names::allow_offgrid_times ) )
  {
    const bool flag_offgrid = d->lookup( names::allow_offgrid_times );

    if ( flag_offgrid != allow_offgrid_times_ and not( times or rate_times_.empty() ) )
    {
      throw BadProperty(
        "Option can only be set together with rate times "
        "or if no rate times have been set." );
    }
    else
    {
      allow_offgrid_times_ = flag_offgrid;
    }
  }

  if ( times xor rates )
  {
    throw BadProperty( "Rate times and values must be reset together." );
  }

  // if neither times or rates are given, return here
  if ( not( times or rates ) )
  {
    return;
  }

  const std::vector< double > d_times = getValue< std::vector< double > >( d->lookup( names::rate_times ) );

  if ( d_times.empty() )
  {
    return;
  }

  if ( d_times.size() != rate_values_.size() )
  {
    throw BadProperty( "Rate times and values have to be the same size." );
  }

  rate_times_.clear();
  rate_times_.reserve( rate_values_.size() );

  // ensure amplitude times are strictly monotonically increasing,
  // align them to the grid if necessary and insert them

  // handle first rate time, and insert it
  std::vector< double >::const_iterator next = d_times.begin();
  assert_valid_rate_time_and_insert( *next );

  // keep track of previous rate
  std::vector< Time >::const_iterator prev_rate = rate_times_.begin();

  // handle all remaining rate times
  for ( ++next; next != d_times.end(); ++next, ++prev_rate )
  {
    assert_valid_rate_time_and_insert( *next );

    // compare the aligned rate times, must be strictly increasing
    if ( *prev_rate >= rate_times_.back() )
    {
      throw BadProperty( "Rate times must be strictly increasing." );
    }
  }

  // reset rate index because we got new data
  b.idx_ = 0;
}

/* ----------------------------------------------------------------
 * Default and copy constructor for node
 * ---------------------------------------------------------------- */

nest::inhomogeneous_poisson_generator::inhomogeneous_poisson_generator()
  : StimulationDevice()
  , P_()
  , B_()
  , V_()
{
}

nest::inhomogeneous_poisson_generator::inhomogeneous_poisson_generator( const inhomogeneous_poisson_generator& n )
  : StimulationDevice( n )
  , P_( n.P_ )
  , B_( n.B_ )
  , V_( n.V_ )
{
}

/* ----------------------------------------------------------------
 * Node initialization functions
 * ---------------------------------------------------------------- */
void
nest::inhomogeneous_poisson_generator::init_state_()
{
  StimulationDevice::init_state();
}

void
nest::inhomogeneous_poisson_generator::init_buffers_()
{
  StimulationDevice::init_buffers();
  B_.idx_ = 0;
  B_.rate_ = 0;
}

void
nest::inhomogeneous_poisson_generator::pre_run_hook()
{
  StimulationDevice::pre_run_hook();
  V_.h_ = Time::get_resolution().get_ms();
}

/* ----------------------------------------------------------------
 * Update function and event hook
 * ---------------------------------------------------------------- */

void
nest::inhomogeneous_poisson_generator::update( Time const& origin, const long from, const long to )
{
  assert( to >= 0 and ( delay ) from < kernel().connection_manager.get_min_delay() );
  assert( from < to );
  assert( P_.rate_times_.size() == P_.rate_values_.size() );

  const long t0 = origin.get_steps();

  // Skip any times in the past. Since we must send events proactively,
  // idx_ must point to times in the future.
  const long first = t0 + from;
  while ( B_.idx_ < P_.rate_times_.size() and P_.rate_times_[ B_.idx_ ].get_steps() <= first )
  {
    ++B_.idx_;
  }

  for ( long offs = from; offs < to; ++offs )
  {
    const long curr_time = t0 + offs;

    // Keep the amplitude up-to-date at all times.
    // We need to change the amplitude one step ahead of time, see comment
    // on class StimulationDevice.
    if ( B_.idx_ < P_.rate_times_.size() and curr_time + 1 == P_.rate_times_[ B_.idx_ ].get_steps() )
    {
      B_.rate_ = P_.rate_values_[ B_.idx_ ] / 1000.0; // scale the rate to ms^-1
      ++B_.idx_;
    }

    // create spikes
    if ( B_.rate_ > 0 and StimulationDevice::is_active( Time::step( curr_time ) ) )
    {
      DSSpikeEvent se;
      kernel().event_delivery_manager.send( *this, se, offs );
    }
  }
}

void
nest::inhomogeneous_poisson_generator::event_hook( DSSpikeEvent& e )
{
  poisson_distribution::param_type param( B_.rate_ * V_.h_ );
  long n_spikes = V_.poisson_dist_( get_vp_specific_rng( get_thread() ), param );

  if ( n_spikes > 0 ) // we must not send events with multiplicity 0
  {
    e.set_multiplicity( n_spikes );
    e.get_receiver().handle( e );
  }
}

/* ----------------------------------------------------------------
 * Other functions
 * ---------------------------------------------------------------- */

void
nest::inhomogeneous_poisson_generator::set_data_from_stimulation_backend( std::vector< double >& rate_time_update )
{
  Parameters_ ptmp = P_; // temporary copy in case of errors
  // For the input backend
  if ( not rate_time_update.empty() )
  {
    if ( rate_time_update.size() % 2 != 0 )
    {
      throw BadParameterValue(
        "The size of the data for the inhomogeneous_poisson_generator needs to be even [(time,rate) pairs]" );
    }
    DictionaryDatum d = DictionaryDatum( new Dictionary );
    std::vector< double > times_ms;
    std::vector< double > rate_values;
    const size_t n_spikes = P_.rate_times_.size();
    for ( size_t n = 0; n < n_spikes; ++n )
    {
      times_ms.push_back( P_.rate_times_[ n ].get_ms() );
      rate_values.push_back( P_.rate_values_[ n ] );
    }
    for ( size_t n = 0; n < rate_time_update.size() / 2; ++n )
    {
      times_ms.push_back( rate_time_update[ n * 2 ] );
      rate_values.push_back( rate_time_update[ n * 2 + 1 ] );
    }
    ( *d )[ names::rate_times ] = DoubleVectorDatum( times_ms );
    ( *d )[ names::rate_values ] = DoubleVectorDatum( rate_values );

    ptmp.set( d, B_, this );
  }

  // if we get here, temporary contains consistent set of properties
  P_ = ptmp;
}
