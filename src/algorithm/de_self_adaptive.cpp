/*****************************************************************************
 *   Copyright (C) 2004-2009 The PaGMO development team,                     *
 *   Advanced Concepts Team (ACT), European Space Agency (ESA)               *
 *   http://apps.sourceforge.net/mediawiki/pagmo                             *
 *   http://apps.sourceforge.net/mediawiki/pagmo/index.php?title=Developers  *
 *   http://apps.sourceforge.net/mediawiki/pagmo/index.php?title=Credits     *
 *   act@esa.int                                                             *
 *                                                                           *
 *   This program is free software; you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation; either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program; if not, write to the                           *
 *   Free Software Foundation, Inc.,                                         *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.               *
 *****************************************************************************/

#include <boost/random/uniform_int.hpp>
#include <boost/random/uniform_real.hpp>
#include <boost/random/normal_distribution.hpp>
#include <string>
#include <vector>

#include "../exceptions.h"
#include "../population.h"
#include "../types.h"
#include "base.h"
#include "de_self_adaptive.h"

namespace pagmo { namespace algorithm {

/// Constructor.
/**
 * Allows to specify in detail all the parameters of the algorithm.
 *
 * @param[in] gen number of generations.
 * @param[in] variant algorithm variant (one of 1..18)
 * @param[in] variant_adptv parameter adaptation scheme to be used (one of 0..1)
 * @param[in] ftol stopping criteria on the x tolerance
 * @param[in] xtol stopping criteria on the f tolerance
 * @param[in] restart when true the algorithm loses memory of the parameter adaptation (if present) at each call
 * @throws value_error if f,cr are not in the [0,1] interval, strategy is not one of 1 .. 10, gen is negative
 */
de_self_adaptive::de_self_adaptive(int gen, int variant, int variant_adptv, double ftol, double xtol, bool restart):base(), m_gen(gen), m_f(0), m_cr(0),
	 m_variant(variant), m_variant_adptv(variant_adptv), m_ftol(ftol), m_xtol(xtol), m_restart(restart) {
	if (gen < 0) {
		pagmo_throw(value_error,"number of generations must be nonnegative");
	}
	if (variant < 1 || variant > 18) {
		pagmo_throw(value_error,"variant index must be one of 1 ... 18");
	}
	if (variant_adptv < 0 || variant_adptv > 1) {
		pagmo_throw(value_error,"adaptive variant index must be one of 0 ... 1");
	}
}

/// Clone method.
base_ptr de_self_adaptive::clone() const
{
	return base_ptr(new de_self_adaptive(*this));
}

/// Evolve implementation.
/**
 * Run the DE algorithm for the number of generations specified in the constructors.
 * At each improvments velocity is also updated.
 *
 * @param[in,out] pop input/output pagmo::population to be evolved.
 */

void de_self_adaptive::evolve(population &pop) const
{
	// Let's store some useful variables.
	const problem::base &prob = pop.problem();
	const problem::base::size_type D = prob.get_dimension(), prob_i_dimension = prob.get_i_dimension(), prob_c_dimension = prob.get_c_dimension(), prob_f_dimension = prob.get_f_dimension();
	const decision_vector &lb = prob.get_lb(), &ub = prob.get_ub();
	const population::size_type NP = pop.size();
	const problem::base::size_type Dc = D - prob_i_dimension;


	//We perform some checks to determine wether the problem/population are suitable for DE
	if ( Dc == 0 ) {
		pagmo_throw(value_error,"There is no continuous part in the problem decision vector for DE to optimise");
	}

	if ( prob_c_dimension != 0 ) {
		pagmo_throw(value_error,"The problem is not box constrained and DE is not suitable to solve it");
	}

	if ( prob_f_dimension != 1 ) {
		pagmo_throw(value_error,"The problem is not single objective and DE is not suitable to solve it");
	}

	if (NP < 8) {
		pagmo_throw(value_error,"for DE Self-Adaptive at least 8 individuals in the population are needed");
	}

	// Get out if there is nothing to do.
	if (m_gen == 0) {
		return;
	}
	// Some vectors used during evolution are allocated here.
	decision_vector dummy(D), tmp(D); //dummy is used for initialisation purposes, tmp to contain the mutated candidate
	std::vector<decision_vector> popold(NP,dummy), popnew(NP,dummy);
	decision_vector gbX(D),gbIter(D);
	fitness_vector newfitness(1);	//new fitness of the mutaded candidate
	fitness_vector gbfit(1);	//global best fitness
	std::vector<fitness_vector> fit(NP,gbfit);

	//We extract from pop the chromosomes and fitness associated
	for (std::vector<double>::size_type i = 0; i < NP; ++i) {
		popold[i] = pop.get_individual(i).cur_x;
		fit[i] = pop.get_individual(i).cur_f;
	}
	popnew = popold;

	// Initialise the global bests
	gbX=pop.champion().x;
	gbfit=pop.champion().f;
	// container for the best decision vector of generation
	gbIter = gbX;
	
	// Initialize the F and CR vectors
	if ( (m_cr.size() != NP) || (m_f.size() != NP) || (m_restart) ) {
		m_cr.resize(NP);
		for (size_t i = 0; i < NP; ++i) {
			m_cr[i] = (m_variant_adptv) ? boost::normal_distribution<double>(0.5,0.15)(m_drng) : boost::uniform_real<double>(0.0,1.0)(m_drng);
		}
		
		m_f.resize(NP);
		for (size_t i = 0; i < NP; ++i){
			m_f[i] = (m_variant_adptv) ? boost::normal_distribution<double>(0.5,0.15)(m_drng) : boost::uniform_real<double>(0.1,1.0)(m_drng);
		}
	}

	// Main DE iterations
	size_t r1,r2,r3,r4,r5,r6,r7;	//indexes to the selected population members
	for (int gen = 0; gen < m_gen; ++gen) {
		//Start of the loop through the deme
		for (size_t i = 0; i < NP; ++i) {
			do {                       /* Pick a random population member */
				/* Endless loop for NP < 2 !!!     */
				r1 = boost::uniform_int<int>(0,NP-1)(m_urng);
			} while (r1==i);

			do {                       /* Pick a random population member */
				/* Endless loop for NP < 3 !!!     */
				r2 = boost::uniform_int<int>(0,NP-1)(m_urng);
			} while ((r2==i) || (r2==r1));

			do {                       /* Pick a random population member */
				/* Endless loop for NP < 4 !!!     */
				r3 = boost::uniform_int<int>(0,NP-1)(m_urng);
			} while ((r3==i) || (r3==r1) || (r3==r2));

			do {                       /* Pick a random population member */
				/* Endless loop for NP < 5 !!!     */
				r4 = boost::uniform_int<int>(0,NP-1)(m_urng);
			} while ((r4==i) || (r4==r1) || (r4==r2) || (r4==r3));

			do {                       /* Pick a random population member */
				/* Endless loop for NP < 6 !!!     */
				r5 = boost::uniform_int<int>(0,NP-1)(m_urng);
			} while ((r5==i) || (r5==r1) || (r5==r2) || (r5==r3) || (r5==r4));
			do {                       /* Pick a random population member */
				/* Endless loop for NP < 7 !!!     */
				r6 = boost::uniform_int<int>(0,NP-1)(m_urng);
			} while ((r6==i) || (r6==r1) || (r6==r2) || (r6==r3) || (r6==r4) || (r6==r5));
			do {                       /* Pick a random population member */
				/* Endless loop for NP < 8 !!!     */
				r7 = boost::uniform_int<int>(0,NP-1)(m_urng);
			} while ((r7==i) || (r7==r1) || (r7==r2) || (r7==r3) || (r7==r4) || (r7==r5) || (r7==r6));

			// Adapt amplification factor and crossover probability if necessary
			double F, CR;
			F = (m_variant_adptv) ? m_f[i] + boost::normal_distribution<double>(0.0,0.5)(m_drng) * (m_f[r1]-m_f[r2])
				   + boost::normal_distribution<double>(0.0,0.5)(m_drng) * (m_f[r3]-m_f[r4])
				   + boost::normal_distribution<double>(0.0,0.5)(m_drng) * (m_f[r5]-m_f[r6]) :
				   ( (m_drng() <0.9) ? m_f[i] : boost::uniform_real<double>(0.1,1.0)(m_drng) );

			CR = (m_variant_adptv) ? m_cr[i] + boost::normal_distribution<double>(0.0,0.5)(m_drng) * (m_cr[r1]-m_cr[r2])
				     + boost::normal_distribution<double>(0.0,0.5)(m_drng) * (m_cr[r3]-m_cr[r4])
				     + boost::normal_distribution<double>(0.0,0.5)(m_drng) * (m_cr[r5]-m_cr[r6]) :
				     ( (m_drng() <0.9) ? m_cr[i] : boost::uniform_real<double>(0.0,1.0)(m_drng) );
			
			/*-------DE/best/1/exp--------------------------------------------------------------------*/
			/*-------Our oldest strategy but still not bad. However, we have found several------------*/
			/*-------optimization problems where misconvergence occurs.-------------------------------*/
			if (m_variant == 1) { /* strategy DE0 (not in our paper) */
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng), L = 0;
				do {
					tmp[n] = gbIter[n] + F*(popold[r2][n]-popold[r3][n]);
					n = (n+1)%Dc;
					++L;
				} while ((m_drng() < CR) && (L < Dc));
			}

			/*-------DE/rand/1/exp-------------------------------------------------------------------*/
			/*-------This is one of my favourite strategies. It works especially well when the-------*/
			/*-------"gbIter[]"-schemes experience misconvergence. Try e.g. m_f=0.7 and m_cr=0.5---------*/
			/*-------as a first guess.---------------------------------------------------------------*/
			else if (m_variant == 2) { /* strategy DE1 in the techreport */
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng), L = 0;
				do {
					tmp[n] = popold[r1][n] + F*(popold[r2][n]-popold[r3][n]);
					n = (n+1)%Dc;
					++L;
				} while ((m_drng() < CR) && (L < Dc));
			}

			/*-------DE/rand-to-best/1/exp-----------------------------------------------------------*/
			/*-------This strategy seems to be one of the best strategies. Try m_f=0.85 and c=1.------*/
			/*-------If you get misconvergence try to increase NP. If this doesn't help you----------*/
			/*-------should play around with all three control variables.----------------------------*/
			else if (m_variant == 3) { /* similiar to DE2 but generally better */
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng), L = 0;
				do {
					tmp[n] = tmp[n] + F*(gbIter[n] - tmp[n]) + F*(popold[r1][n]-popold[r2][n]);
					n = (n+1)%Dc;
					++L;
				} while ((m_drng() < CR) && (L < Dc));
			}
			/*-------DE/best/2/exp is another powerful strategy worth trying--------------------------*/
			else if (m_variant == 4) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng), L = 0;
				do {
					tmp[n] = gbIter[n] +
						 (popold[r1][n]+popold[r2][n]-popold[r3][n]-popold[r4][n])*F;
					n = (n+1)%Dc;
					++L;
				} while ((m_drng() < CR) && (L < Dc));
			}
			/*-------DE/rand/2/exp seems to be a robust optimizer for many functions-------------------*/
			else if (m_variant == 5) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng), L = 0;
				do {
					tmp[n] = popold[r5][n] +
						 (popold[r1][n]+popold[r2][n]-popold[r3][n]-popold[r4][n])*F;
					n = (n+1)%Dc;
					++L;
				} while ((m_drng() < CR) && (L < Dc));
			}

			/*=======Essentially same strategies but BINOMIAL CROSSOVER===============================*/

			/*-------DE/best/1/bin--------------------------------------------------------------------*/
			else if (m_variant == 6) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng);
				for (size_t L = 0; L < Dc; ++L) { /* perform Dc binomial trials */
					if ((m_drng() < CR) || L + 1 == Dc) { /* change at least one parameter */
						tmp[n] = gbIter[n] + F*(popold[r2][n]-popold[r3][n]);
					}
					n = (n+1)%Dc;
				}
			}
			/*-------DE/rand/1/bin-------------------------------------------------------------------*/
			else if (m_variant == 7) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng);
				for (size_t L = 0; L < Dc; ++L) { /* perform Dc binomial trials */
					if ((m_drng() < CR) || L + 1 == Dc) { /* change at least one parameter */
						tmp[n] = popold[r1][n] + F*(popold[r2][n]-popold[r3][n]);
					}
					n = (n+1)%Dc;
				}
			}
			/*-------DE/rand-to-best/1/bin-----------------------------------------------------------*/
			else if (m_variant == 8) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng);
				for (size_t L = 0; L < Dc; ++L) { /* perform Dc binomial trials */
					if ((m_drng() < CR) || L + 1 == Dc) { /* change at least one parameter */
						tmp[n] = tmp[n] + F*(gbIter[n] - tmp[n]) + F*(popold[r1][n]-popold[r2][n]);
					}
					n = (n+1)%Dc;
				}
			}
			/*-------DE/best/2/bin--------------------------------------------------------------------*/
			else if (m_variant == 9) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng);
				for (size_t L = 0; L < Dc; ++L) { /* perform Dc binomial trials */
					if ((m_drng() < CR) || L + 1 == Dc) { /* change at least one parameter */
						tmp[n] = gbIter[n] +
							 (popold[r1][n]+popold[r2][n]-popold[r3][n]-popold[r4][n])*F;
					}
					n = (n+1)%Dc;
				}
			}
			/*-------DE/rand/2/bin--------------------------------------------------------------------*/
			else if (m_variant == 10) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng);
				for (size_t L = 0; L < Dc; ++L) { /* perform Dc binomial trials */
					if ((m_drng() < CR) || L + 1 == Dc) { /* change at least one parameter */
						tmp[n] = popold[r5][n] +
							 (popold[r1][n]+popold[r2][n]-popold[r3][n]-popold[r4][n])*F;
					}
					n = (n+1)%Dc;
				}
			}

			/*-------DE/best/3/exp--------------------------------------------------------------------*/
			if (m_variant == 11) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng), L = 0;
				do {
					tmp[n] = gbIter[n] + F*(popold[r1][n]-popold[r2][n]) + F*(popold[r3][n]-popold[r4][n]) + F*(popold[r5][n]-popold[r6][n]);
					n = (n+1)%Dc;
					++L;
				} while ((m_drng() < CR) && (L < Dc));
			}
			/*-------DE/best/3/bin--------------------------------------------------------------------*/
			else if (m_variant == 12) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng);
				for (size_t L = 0; L < Dc; ++L) { /* perform Dc binomial trials */
					if ((m_drng() < CR) || L + 1 == Dc) { /* change at least one parameter */
						tmp[n] = gbIter[n] + F*(popold[r1][n]-popold[r2][n]) + F*(popold[r3][n]-popold[r4][n]) + F*(popold[r5][n]-popold[r6][n]);
					}
					n = (n+1)%Dc;
				}
			}
			/*-------DE/rand/3/exp--------------------------------------------------------------------*/
			if (m_variant == 13) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng), L = 0;
				do {
					tmp[n] = popold[r7][n] + F*(popold[r1][n]-popold[r2][n]) + F*(popold[r3][n]-popold[r4][n]) + F*(popold[r5][n]-popold[r6][n]);
					n = (n+1)%Dc;
					++L;
				} while ((m_drng() < CR) && (L < Dc));
			}
			/*-------DE/rand/3/bin--------------------------------------------------------------------*/
			else if (m_variant == 14) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng);
				for (size_t L = 0; L < Dc; ++L) { /* perform Dc binomial trials */
					if ((m_drng() < CR) || L + 1 == Dc) { /* change at least one parameter */
						tmp[n] = popold[r7][n] + F*(popold[r1][n]-popold[r2][n]) + F*(popold[r3][n]-popold[r4][n]) + F*(popold[r5][n]-popold[r6][n]);
					}
					n = (n+1)%Dc;
				}
			}
			/*-------DE/rand-to-current/2/exp---------------------------------------------------------*/
			if (m_variant == 15) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng), L = 0;
				do {
					tmp[n] = popold[r7][n] + F*(popold[r1][n]-popold[i][n]) + F*(popold[r3][n]-popold[r4][n]);
					n = (n+1)%Dc;
					++L;
				} while ((m_drng() < CR) && (L < Dc));
			}
			/*-------DE/rand-to-current/2/bin---------------------------------------------------------*/
			else if (m_variant == 16) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng);
				for (size_t L = 0; L < Dc; ++L) { /* perform Dc binomial trials */
					if ((m_drng() < CR) || L + 1 == Dc) { /* change at least one parameter */
						tmp[n] = popold[r7][n] + F*(popold[r1][n]-popold[i][n]) + F*(popold[r3][n]-popold[r4][n]);
					}
					n = (n+1)%Dc;
				}
			}
			/*-------DE/rand-to-best-and-current/2/exp------------------------------------------------*/
			if (m_variant == 17) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng), L = 0;
				do {
					tmp[n] = popold[r7][n] + F*(popold[r1][n]-popold[i][n]) + F*(gbIter[n]-popold[r4][n]);
					n = (n+1)%Dc;
					++L;
				} while ((m_drng() < CR) && (L < Dc));
			}
			/*-------DE/rand-to-best-and-current/2/bin------------------------------------------------*/
			else if (m_variant == 18) {
				tmp = popold[i];
				size_t n = boost::uniform_int<int>(0,Dc-1)(m_urng);
				for (size_t L = 0; L < Dc; ++L) { /* perform Dc binomial trials */
					if ((m_drng() < CR) || L + 1 == Dc) { /* change at least one parameter */
						tmp[n] = popold[r7][n] + F*(popold[r1][n]-popold[i][n]) + F*(gbIter[n]-popold[r4][n]);
					}
					n = (n+1)%Dc;
				}
			}

			/*=======Trial mutation now in tmp[]. force feasibility and how good this choice really was.==================*/
			// a) feasibility
			size_t i2 = 0;
			while (i2<Dc) {
				if ((tmp[i2] < lb[i2]) || (tmp[i2] > ub[i2]))
					tmp[i2] = boost::uniform_real<double>(lb[i2],ub[i2])(m_drng);
				++i2;
			}

			//b) how good?
			prob.objfun(newfitness, tmp);    /* Evaluate new vector in tmp[] */
			if ( pop.problem().compare_fitness(newfitness,fit[i]) ) {  /* improved objective function value ? */
				fit[i]=newfitness;
				popnew[i] = tmp;
				
				// Update the adapted parameters
				m_cr[i] = CR;
				m_f[i] = F;
				
				// As a fitness improvment occured we move the point
				// and thus can evaluate a new velocity
				std::transform(tmp.begin(), tmp.end(), pop.get_individual(i).cur_x.begin(), tmp.begin(),std::minus<double>());
				
				//updates x and v (cache avoids to recompute the objective function)
				pop.set_x(i,popnew[i]);
				pop.set_v(i,tmp);
				if ( pop.problem().compare_fitness(newfitness,gbfit) ) {
					/* if so...*/
					gbfit=newfitness;          /* reset gbfit to new low...*/
					gbX=tmp;
				}
			} else {
				popnew[i] = popold[i];
			}

		}//End of the loop through the deme

		/* Save best population member of current iteration */
		gbIter = gbX;

		/* swap population arrays. New generation becomes old one */
		std::swap(popold, popnew);


		//9 - Check the exit conditions (every 40 generations)
		if (gen%40) {
			double dx = 0;
			for (decision_vector::size_type i = 0; i < D; ++i) {
				tmp[i] = pop.get_individual(pop.get_worst_idx()).best_x[i] - pop.get_individual(pop.get_best_idx()).best_x[i];
				dx += std::fabs(tmp[i]);
			}
			
			if  ( dx < m_xtol ) {
				if (m_screen_output) { 
					std::cout << "Exit condition -- xtol < " <<  m_xtol << std::endl;
				}
				return;
			}

			double mah = std::fabs(pop.get_individual(pop.get_worst_idx()).best_f[0] - pop.get_individual(pop.get_best_idx()).best_f[0]);

			if (mah < m_ftol) {
				if (m_screen_output) {
					std::cout << "Exit condition -- ftol < " <<  m_ftol << std::endl;
				}
				return;
			}
		}
		

	}//end main DE iterations
	if (m_screen_output) {
		std::cout << "Exit condition -- generations > " <<  m_gen << std::endl;
	}

}

/// Algorithm name
std::string de_self_adaptive::get_name() const
{
	return "DE - Self adaptive";
}

/// Extra human readable algorithm info.
/**
 * @return a formatted string displaying the parameters of the algorithm.
 */
std::string de_self_adaptive::human_readable_extra() const
{
	std::ostringstream s;
	s << "gen:" << m_gen << ' ';
	s << "variant:" << m_variant << ' ';
	s << "ftol:" << m_ftol << ' ';
	s << "xtol:" << m_xtol << ' ';
	s << "restart:" << m_restart;
	return s.str();
}

}} //namespaces

BOOST_CLASS_EXPORT_IMPLEMENT(pagmo::algorithm::de_self_adaptive);
