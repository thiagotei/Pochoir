/*
 * ============================================================================
 *       Filename:  pochoir_modified_cuts.hpp
 *    Description:  Has routines 
 *					1. that implement the modified space/power of two
 *					time cuts.
 *					The code uses the time/space cut code framework in
 *					pochoir_walk_recursive.hpp
 *        Created:  10/02/2012
 *         Author:  Eka Palamadai, epn@mit.edu
 * ============================================================================
 */
#ifndef AUTO_TUNING_ARBITRARY_CUTS_TRAP_HPP 
#define AUTO_TUNING_ARBITRARY_CUTS_TRAP_HPP 

//#include "auto_tuning_arbitrary_cuts_header.hpp"
#include "auto_tuning_homogeneous_header.hpp"

#define dx_recursive_boundary_  (m_algo.dx_recursive_boundary_)
#define dx_recursive_ (m_algo.dx_recursive_)
#define dt_recursive_boundary_ (m_algo.dt_recursive_boundary_)
#define dt_recursive_ (m_algo.dt_recursive_)
#define slope_ m_algo.slope_
#define touch_boundary m_algo.touch_boundary
#define phys_length_ m_algo.phys_length_
#define base_case_kernel_boundary m_algo.base_case_kernel_boundary
#define base_case_kernel_boundary_rectangle m_algo.base_case_kernel_boundary_rectangle
#define uub_boundary m_algo.uub_boundary
#define ulb_boundary m_algo.ulb_boundary
#define lub_boundary m_algo.lub_boundary

template <int N_RANK> template <typename F>
inline void auto_tune<N_RANK>::symbolic_trap_space_cut_interior(int t0,
int t1, grid_info<N_RANK> const & grid, unsigned long parent_index, 
F const & f, int * num_zoids, double & redundant_time, double & projected_time,
double & max_loop_time, int bits)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

	int child_index = 0 ;
    // set up the initial grid 
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                // spawn all the grids in circular_queue_[curr_dep][] 
#if USE_CILK_FOR 
                // use cilk_for to spawn all the sub-grid 
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    // assert all the sub-grid has done N_RANK spatial cuts 
                    assert(l_son->level == -1);
                    symbolic_trap_space_time_cut_interior(l_son->t0, 
						l_son->t1, l_son->grid, parent_index, child_index, 
						redundant_time, projected_time, f);
					child_index++ ;
                } // end cilk_for 
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                // use cilk_spawn to spawn all the sub-grid 
                pop_queue(curr_dep_pointer);
				double time = 0 ;
				symbolic_trap_space_time_cut_interior(l_father->t0, 
					l_father->t1, l_father->grid, parent_index, child_index, 
					redundant_time, projected_time, f, time);
				max_loop_time = max(time, max_loop_time) ;
				child_index++ ;
#endif
            } else {
                // performing a space cut on dimension 'level' 
                pop_queue(curr_dep_pointer);
                const grid_info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int thres = slope_[level] * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
				int cut_dim = bits & 1 << level ;
                if (num_zoids [level] == 0 || ! cut_dim) {
                    // if we can't cut into this dimension, just directly push 
                    // it into the circular queue 
                    push_queue(curr_dep_pointer, level-1, t0, t1, 
							l_father_grid) ;
                } else {
                    // can_cut! 
                    if (cut_lb) {
                        const int mid = (lb/2);
                        grid_info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        // push the middle triangular minizoid (gray) into 
                        // circular queue of (curr_dep) 
                        //
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = slope_[level];
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -slope_[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        // cilk_sync 
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        // push the left big trapezoid (black)
                        // into circular queue of (curr_dep + 1)
                        //
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = slope_[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        // push the right big trapezoid (black)
                        // into circular queue of (curr_dep + 1)
                        //
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -slope_[level];
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                    } // end if (cut_lb) 
                    else {
                        // cut_tb 
                        const int mid = (tb/2);
                        grid_info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);
                        const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);

                        // push left black sub-grid into circular queue of (curr_dep) 
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = -slope_[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        // push right black sub-grid into circular queue of (curr_dep) 
                        l_son_grid.x0[level] = ul_start + mid;;
                        l_son_grid.dx0[level] = slope_[level];
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        // cilk_sync 
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        // push the middle gray triangular minizoid into 
                        // circular queue of (curr_dep + 1)
                        //
                        l_son_grid.x0[level] = ul_start + mid;
                        l_son_grid.dx0[level] = -slope_[level];
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = slope_[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } // end else (cut_tb) 
                }// end if (can_cut) 
            } // end if (performing a space cut) 
        } // end while (queue_len_[curr_dep] > 0) 
#if !USE_CILK_FOR
//        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } // end for (curr_dep < N_RANK+1) 
}

/* Boundary space cut. Uses trap space cut.
 */
template <int N_RANK> template <typename F, typename BF> 
inline void auto_tune<N_RANK>::symbolic_trap_space_cut_boundary(int t0,
		int t1, grid_info<N_RANK> const & grid, unsigned long parent_index, 
		F const & f, BF const & bf, int * num_zoids, double & redundant_time, 
		double & projected_time, double & max_loop_time, int bits)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }
	
	int child_index = 0 ;
    // set up the initial grid 
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                // spawn all the grids in circular_queue_[curr_dep][] 
#if USE_CILK_FOR 
                // use cilk_for to spawn all the sub-grid 
// #pragma cilk_grainsize = 1
				
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    // assert all the sub-grid has done N_RANK spatial cuts 
                    //assert(l_son->level == -1);
                    symbolic_trap_space_time_cut_boundary(l_son->t0, 
						l_son->t1, l_son->grid, parent_index, child_index, 
						redundant_time, projected_time, f, bf);
					child_index++ ; //this can be a race.
                } // end cilk_for 
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                // use cilk_spawn to spawn all the sub-grid 
                pop_queue(curr_dep_pointer);
				double time = 0 ;
				symbolic_trap_space_time_cut_boundary(l_father->t0, 
					l_father->t1, l_father->grid, parent_index, child_index,
					redundant_time, projected_time, f, bf, time) ;
				max_loop_time = max(time, max_loop_time) ;
				child_index++ ; 
#endif
            } else {
                // performing a space cut on dimension 'level' 
                pop_queue(curr_dep_pointer);
                grid_info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int lt = (t1 - t0);
                const int level = l_father->level;
                const int thres = slope_[level] * lt;
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                const bool cut_lb = (lb < tb);
				int cut_dim = bits & 1 << level ;
				//cout << "level " << level << "cut_dim " << cut_dim << endl ;
                if (num_zoids [level] == 0 || ! cut_dim) {
                    // if we can't cut into this dimension, just directly push
                    // it into the circular queue
                    push_queue(curr_dep_pointer, level-1, t0, t1, 
							l_father_grid) ;
                } else  {
                    // can_cut 
                    if (cut_lb) {
                        // if cutting lb, there's no initial cut! 
                        assert(lb != phys_length_[level] || l_father_grid.dx0[level] != 0 || l_father_grid.dx1[level] != 0);
                        const int mid = lb/2;
                        grid_info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        // push the middle gray minizoid
                        // into circular queue of (curr_dep) 
                        //
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = slope_[level];
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -slope_[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        // cilk_sync 
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        // push one sub-grid into circular queue of (curr_dep + 1)
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = slope_[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        // push one sub-grid into circular queue of (curr_dep + 1)
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -slope_[level];
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } // end if (cut_lb) 
                    else { // cut_tb 
                        if (lb == phys_length_[level] && l_father_grid.dx0[level] == 0 && l_father_grid.dx1[level] == 0) { /* initial cut on the dimension */
						   /* initial cut on the dimension */
                            assert(l_father_grid.dx0[level] == 0);
                            assert(l_father_grid.dx1[level] == 0);
                            const int mid = tb/2;
							const int dx = slope_ [level] * lt ;
                            grid_info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);

                            l_son_grid.x0[level] = l_start ;
                            l_son_grid.dx0[level] = slope_[level];
                            l_son_grid.x1[level] = l_end ;
                            l_son_grid.dx1[level] = -slope_[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);
							
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            // push triangle into circular queue of (curr_dep + 1)
                            l_son_grid.x0[level] = l_end ;
                            l_son_grid.dx0[level] = -slope_[level];
                            l_son_grid.x1[level] = l_end ;
                            l_son_grid.dx1[level] = slope_[level];
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
							/*
                            //draw a triangle with a vertex at midpoint of 
							//top base.
                            l_son_grid.x0[level] = mid - dx ;
                            l_son_grid.dx0[level] = slope_[level];
                            l_son_grid.x1[level] = mid + dx ;
                            l_son_grid.dx1[level] = -slope_[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            // cilk_sync
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            // push trapezoid into circular queue of (curr_dep + 1)
                            l_son_grid.x0[level] = mid + dx ;
                            l_son_grid.dx0[level] = -slope_[level];
                            l_son_grid.x1[level] = l_end + mid - dx ;
                            l_son_grid.dx1[level] = slope_[level];
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
							*/
                        } else { /* NOT the initial cut! */
                            const int mid = tb/2;
                            grid_info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            // push one sub-grid into circular queue of (curr_dep) 
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = -slope_[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            // push one sub-grid into circular queue of (curr_dep) 
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = slope_[level];
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            // cilk_sync 
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            // push one sub-grid into circular queue of (curr_dep + 1)
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -slope_[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = slope_[level];
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }                    
                    } // end if (cut_tb) 
                } // end if (can_cut) 
            } // end if (performing a space cut) 
        } // end while (queue_len_[curr_dep] > 0) 
//#if !USE_CILK_FOR
//        cilk_sync;
//#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } // end for (curr_dep < N_RANK+1) 
}

template <int N_RANK> template <typename F>
inline void auto_tune<N_RANK>::symbolic_trap_space_time_cut_interior(
		int t0, int t1, grid_info<N_RANK> const & grid, 
		unsigned long parent_index, int child_index, 
		double & redundant_time, double & projected_time, F const & f,
		double & max_loop_time)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false;
    grid_info<N_RANK> l_son_grid;

	struct timespec start, end;
	struct timespec start1, end1 ;
	clock_gettime(CLOCK_MONOTONIC, &start);

	int centroid = 0, width = 1 ; 
	int total_num_subzoids = 1 ;
	int num_subzoids [N_RANK] ;
	unsigned long key = 0 ;
	unsigned short decision = 0 ;
    for (int i = N_RANK-1; i >= 0; --i) {
        unsigned long lb, tb;
        int thres ;
        lb = (grid.x1[i] - grid.x0[i]);
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
		//centroid = pmod(grid.x0[i] + (lb >> 1), phys_length_ [i]) * width + 
		centroid = pmod(grid.x0[i], phys_length_ [i]) * width + 
					centroid ;
		assert (centroid >= 0) ;
		width *= phys_length_ [i] ;

		key <<= num_bits_width ;
		key |= lb ;
		key <<= num_bits_width ;
		key |= tb ;
		/*cout << " x0 [" << i << "] " << grid.x0 [i] 
			 << " x1 [" << i << "] " << grid.x1 [i] 
			<< " x2 [" << i << "] " << grid.x0[i] + grid.dx0[i] * lt
			<< " x3 [" << i << "] " << grid.x1[i] + grid.dx1[i] * lt
			<< " lt " << lt << endl ;*/
        thres = slope_[i] * lt ;
		int short_side ;
		bool space_cut = false ;
		if (lb < tb)
		{
			short_side = lb ;
			//set if projection trapezoid is inverted
			decision |= 1 << i + 1 + N_RANK ;
		}
		else
		{
			short_side = tb ;
		}
		num_subzoids [i] = 0 ;
		if (short_side >= (thres << 1) && lb > dx_recursive_[i])
		//if (short_side >= 2 * thres)
		{
			space_cut = true ;
			//set if a space cut can happen in dimension i
			decision |= 1 << i + 1 ;
			num_subzoids [i] = 3 ;
			total_num_subzoids *= num_subzoids [i] ;
		}
        sim_can_cut |= space_cut ;
    }
	unsigned long index ;
	bool projection_exists = check_and_create_projection_interior (key, lt, 
									index, grid) ;
	zoid_type & z = m_zoids [index];
	zoid_type & parent = m_zoids [parent_index] ;
	//add the zoid as a child of the parent
	parent.add_child(&z, child_index, index) ;
	assert (index < m_zoids.size()) ;
	
	if (projection_exists)
	{ 
		//Add the projected time of the zoid to that of the parent.
		clock_gettime(CLOCK_MONOTONIC, &end) ;
		redundant_time += tdiff2(&end, &start) ;
		projected_time += z.time  ;
		max_loop_time = z.max_loop_time ;
		//cout << " zoid  " << index << " exists " << endl ;
		//a zoid with the projection already exists. return
		assert (z.decision >> zoid_type::NUM_BITS_DECISION - 1 == 0) ;
		assert (z.height == lt) ;
#ifndef NDEBUG
		/*grid_info<N_RANK> & grid2 = z.info ;
		int h = z.height ;
		for (int i = N_RANK - 1 ; i >= 0 ; --i)
		{
			cout << " x0 [" << i << "] " << grid2.x0 [i] 
				 << " x1 [" << i << "] " << grid2.x1 [i] 
				<< " x2 [" << i << "] " << grid2.x0[i] + grid2.dx0[i] * h
				<< " x3 [" << i << "] " << grid2.x1[i] + grid2.dx1[i] * h
				<< " h " << h << endl ;
		}*/
#endif
		return ;
	}

	bool divide_and_conquer = false ;
	double projected_time1 = 0, necessary_time = 0 ;
	double time_cut_elapsed_time = 0, space_cut_elapsed_time = ULONG_MAX ; 
	double time_cut_ptime = 0, space_cut_ptime = 0 ;
	double time_cut_rtime = 0, space_cut_rtime = 0 ;
	if (lt > dt_recursive_)
	{
		divide_and_conquer = true ;
		//m_zoids [index].resize_children(max (2, total_num_subzoids)) ;
		m_zoids [index].set_capacity(max (2, total_num_subzoids)) ;
		m_zoids [index].resize_children(2) ;
	}
	clock_gettime(CLOCK_MONOTONIC, &start1);
	if (lt > dt_recursive_)
	{
		double time1 = 0, time2 = 0 ;
        /* cut into time */
        int halflt = lt / 2;
        l_son_grid = grid;
        symbolic_trap_space_time_cut_interior(t0, t0+halflt, l_son_grid, 
				index, 0, time_cut_rtime, time_cut_ptime, f, time1);

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = grid.dx0[i];
            l_son_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = grid.dx1[i];
        }
        symbolic_trap_space_time_cut_interior(t0+halflt, t1, l_son_grid, 
				index, 1, time_cut_rtime, time_cut_ptime, f, time2);
		time1 = max(time1, time2) ;
		max_loop_time = max(time1, max_loop_time) ;
    }
	clock_gettime(CLOCK_MONOTONIC, &end1);
	time_cut_elapsed_time = tdiff2(&end1, &start1) - time_cut_rtime ;
	assert (time_cut_elapsed_time >= 0.) ;
	assert (time_cut_ptime >= 0.) ;
	assert (time_cut_rtime >= 0.) ;
#ifndef NDEBUG
	if (lt > dt_recursive_)
	{
		m_zoids [index].ttime = time_cut_elapsed_time + time_cut_ptime ;
	}
#endif
	zoid_type bak ;
    if (sim_can_cut) 
	{
		assert (decision) ;
		divide_and_conquer = true ; 
		if (lt > dt_recursive_)
		{
			//back up the time cut children data
			bak = m_zoids [index] ;
			assert(bak.num_children == 2) ;
			//bak.resize_children(2) ;
		}
		//m_zoids [index].resize_children(total_num_subzoids) ;
		m_zoids [index].set_capacity(total_num_subzoids) ;
	}
	
	//set num_children correctly before recursing down to child.
	if (sim_can_cut) 
	{
		zoid_type bak2 ;
		int num_cases = 1 << N_RANK ;
		int best_case = 0, num_children_best_case = 1 ;
		for (int i = num_cases - 1 ; i > 0 ; i--)
		{
			int invalid_case = 0 ;
			int num_children = 1 ;
			for (int j = 0 ; j < N_RANK && ! invalid_case ; j++)
			{
				int like_to_cut_dim_j = i & 1 << j ;
				int can_cut_dim_j = decision & 1 << j + 1 ;
				//if we like to cut dim j but cannot cut dim j,
				//the case is invalid.
				invalid_case = like_to_cut_dim_j && ! can_cut_dim_j ;
				//find # of subzoids in dim j
				if (like_to_cut_dim_j && can_cut_dim_j)
				{
					num_children *= 3 ;
				}
			}
			if (invalid_case)
			{
				//invalid case.
				continue ;
			}
			assert (num_children <= total_num_subzoids) ;
			m_zoids [index].resize_children(num_children) ;
			clock_gettime(CLOCK_MONOTONIC, &start1);
			/* cut into space */
			double time = 0, rtime = 0, ptime = 0, elapsed_time = 0  ;
			symbolic_trap_space_cut_interior(t0, t1, grid, index, f, 
							num_subzoids, rtime, ptime,
							//num_subzoids, space_cut_rtime, space_cut_ptime,
							time, i) ;
			//decision = 2 ;
			clock_gettime(CLOCK_MONOTONIC, &end1);
			max_loop_time = max(time, max_loop_time) ;
			//space_cut_elapsed_time = tdiff2(&end1, &start1) - space_cut_rtime ;
			elapsed_time = tdiff2(&end1, &start1) - rtime ;
			assert (elapsed_time >= 0.) ;
			assert (ptime >= 0.) ;
			assert (rtime >= 0.) ;
			if (elapsed_time + ptime < space_cut_elapsed_time + space_cut_ptime)
			{
				space_cut_elapsed_time = elapsed_time ;
				space_cut_ptime = ptime ;
				best_case = i ;
				//back up the zoid with its children.
				bak2 = m_zoids [index] ;
				assert (m_zoids [index].num_children == num_children) ;
				assert (m_zoids [index].num_children <= total_num_subzoids) ;
				num_children_best_case = num_children ;
			}
		}
		assert (space_cut_elapsed_time >= 0.) ;
		assert (space_cut_ptime >= 0.) ;
		assert (space_cut_rtime >= 0.) ;

		//set the decision with the best case found
		for (int j = 0 ; j < N_RANK ; j++)
		{
			int bit = best_case & 1 << j ;
			decision = decision & ~(1 << j + 1) | (bit != 0) << j + 1 ;
#ifndef NDEBUG
			int decision_bit = decision & 1 << j + 1 ;
			assert (decision_bit == bit << 1) ;
#endif
		}
		//restore the back up.
		m_zoids [index] = bak2 ;
		//m_zoids [index].resize_children(num_children) ;
		assert (m_zoids [index].num_children == num_children_best_case) ;
		assert (m_zoids [index].num_children <= total_num_subzoids) ;
	}
#ifndef NDEBUG
	if (sim_can_cut)
	{
		m_zoids [index].stime = space_cut_elapsed_time + space_cut_ptime ;
	}
#endif
	if (lt > dt_recursive_ && sim_can_cut)
	{
		if (space_cut_elapsed_time + space_cut_ptime < time_cut_elapsed_time + 														time_cut_ptime)
		{
			//space cut is better
			projected_time1 = space_cut_ptime ;
			necessary_time = space_cut_elapsed_time ;
			//decision is already set in the initial loop above for space cut.
			//cout << "choosing space cut for zoid " << m_zoids [index].id << endl ;
		}
		else
		{
			//time cut is better
			projected_time1 = time_cut_ptime ;
			necessary_time = time_cut_elapsed_time ;
			decision = 1 ;
			m_zoids [index] = bak ; //restore the backup
			assert (m_zoids [index].num_children == 2) ;
			//cout << "choosing time cut for zoid " << m_zoids [index].id << endl ;
		}
	}
	else if (lt > dt_recursive_)
	{
		//time cut is the only choice
		projected_time1 = time_cut_ptime ;
		necessary_time = time_cut_elapsed_time ;
		decision = 1 ;
		assert (m_zoids [index].num_children == 2) ;
		//cout << "choosing time cut for zoid " << m_zoids [index].id << endl ;
	}
	else if (sim_can_cut)
	{
		//space cut is the only choice
		projected_time1 = space_cut_ptime ;
		necessary_time = space_cut_elapsed_time ;
		//decision is already set in the initial loop above for space cut.
		//cout << "choosing space cut for zoid " << m_zoids [index].id << endl ;
	}
	
    //base case
	//suppose loop_time(z) >= loop_time(z'), z' \in tree(z)
	//if divide_and_conquer_time(z) < max_{z' \in tree(z)} loop_time(z')
	//	then avoid computing loop_time(z).
	//else compute loop_time(z).
	double loop_time = 0 ;
	if (divide_and_conquer && necessary_time + projected_time1 < max_loop_time)
	{
		//do not compute loop_time.
		m_zoids [index].decision |= decision ;
		projected_time += projected_time1 ;
		m_zoids [index].time = necessary_time + projected_time1 ;
	}
	else 
	{
		assert (! divide_and_conquer || 
		(divide_and_conquer && necessary_time+projected_time1 >=max_loop_time));
		//determine the looping time on the zoid
		clock_gettime(CLOCK_MONOTONIC, &start1) ;
		f(t0, t1, grid);
		clock_gettime(CLOCK_MONOTONIC, &end1) ;
		loop_time = tdiff2(&end1, &start1) ;
#ifndef NDEBUG
		m_zoids [index].ltime = loop_time ;
#endif
		max_loop_time = max(loop_time, max_loop_time) ;
		
		//store the decision for the zoid and pass the redundant time 
		//to the parent
		if (divide_and_conquer && 
			necessary_time + projected_time1 < zoid_type::FUZZ * loop_time)
		{
			m_zoids [index].decision |= decision ;
			projected_time += projected_time1 ;
			m_zoids [index].time = necessary_time + projected_time1 ;
		}
		else
		{
			m_zoids [index].decision = 0 ;
			necessary_time = loop_time ;
			m_zoids [index].time = loop_time ;
		}
	}
	m_zoids [index].max_loop_time = max_loop_time ;
	
	clock_gettime(CLOCK_MONOTONIC, &end);
	double total_time = tdiff2(&end, &start) ;
	redundant_time += total_time - necessary_time ;
}

template <int N_RANK> template <typename F, typename BF>
inline void auto_tune<N_RANK>::symbolic_trap_space_time_cut_boundary(
int t0, int t1,	grid_info<N_RANK> const & grid, 
unsigned long parent_index, int child_index, 
double & redundant_time, double & projected_time, F const & f, BF const & bf,
double & max_loop_time)
{
    const int lt = t1 - t0;
    bool sim_can_cut = false, call_boundary = false;
    grid_info<N_RANK> l_father_grid = grid, l_son_grid;
    int l_dt_stop;

	int centroid = 0, width = 1 ; 
	int total_num_subzoids = 1 ;
	int num_subzoids [N_RANK] ;
	unsigned long key = 0 ;
	unsigned short decision = 0 ;

	struct timespec start, end;
	struct timespec start1, end1 ;
	clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = N_RANK-1; i >= 0; --i) {
        unsigned long lb, tb;
        int thres ;
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid);
        lb = (grid.x1[i] - grid.x0[i]);
		//centroid = pmod(grid.x0[i] + (lb >> 1), phys_length_ [i]) * width + 
		centroid = pmod(grid.x0[i], phys_length_ [i]) * width + 
					centroid ;
		assert (centroid >= 0) ;
		width *= phys_length_ [i] ;
        tb = (grid.x1[i] + grid.dx1[i] * lt - grid.x0[i] - grid.dx0[i] * lt);
		/*cout << " x0 [" << i << "] " << grid.x0 [i] 
			 << " x1 [" << i << "] " << grid.x1 [i] 
			<< " x2 [" << i << "] " << grid.x0[i] + grid.dx0[i] * lt
			<< " x3 [" << i << "] " << grid.x1[i] + grid.dx1[i] * lt
			<< " lt " << lt << endl ;*/
        thres = slope_[i] * lt ;
        if (lb == phys_length_[i] && grid.dx0[i] == 0 && grid.dx1[i] == 0) 
		{ 
			//set if initial cut on the dimension 
			decision |= 1 << i + 1 + 3 * N_RANK ;
		}
		int short_side ;
		bool space_cut = false ;
		if (lb < tb)
		{
			short_side = lb ;
			//set if projection trapezoid is inverted
			decision |= 1 << i + 1 + N_RANK ;
		}
		else
		{
			short_side = tb ;
		}
		int limit ;
		if (l_touch_boundary)
		{
			limit = dx_recursive_boundary_[i] ;
		}
		else
		{
			limit = dx_recursive_[i] ;
		}
		num_subzoids [i] = 0 ;
		if (short_side >= (thres << 1) && lb > limit)
		//if (short_side >= 2 * thres) 
		{
			space_cut = true ;
			//set if a space cut can be done in dimension i
			decision |= 1 << i + 1 ;
			num_subzoids [i] = 3 ;
			total_num_subzoids *= num_subzoids [i] ;
		}
        sim_can_cut |= space_cut ;

		key <<= num_bits_width ;
		key |= lb ;
		key <<= num_bits_width ;
		key |= tb ;

        call_boundary |= l_touch_boundary;
    }
	unsigned long index ;
	bool projection_exists = false ;
	if (call_boundary)
	{
		projection_exists = check_and_create_projection_boundary (key, lt, 
								centroid, index, l_father_grid) ;
	}
	else
	{
		projection_exists = check_and_create_projection_interior (key, lt, 
									index, l_father_grid) ;
	}
	zoid_type & z = m_zoids [index] ;
	zoid_type & parent = m_zoids [parent_index] ;
	//add the zoid as a child of the parent
	parent.add_child(&z, child_index, index) ;
	assert (index < m_zoids.size()) ;
	if (projection_exists)
	{ 
		//Add the time of the zoid to the parent.
		clock_gettime(CLOCK_MONOTONIC, &end) ;
		redundant_time += tdiff2(&end, &start) ;
		projected_time += z.time  ;
		max_loop_time = z.max_loop_time ;
		//cout << " zoid exists " << endl ;
		//a zoid with the projection already exists. return
		assert (z.decision >> zoid_type::NUM_BITS_DECISION - 1 ==call_boundary);
		assert (z.height == lt) ;
#ifndef NDEBUG
		/*grid_info<N_RANK> & grid2 = z.info ;
		int h = z.height ;
		for (int i = N_RANK - 1 ; i >= 0 ; --i)
		{
			cout << " x0 [" << i << "] " << grid2.x0 [i] 
				 << " x1 [" << i << "] " << grid2.x1 [i] 
				<< " x2 [" << i << "] " << grid2.x0[i] + grid2.dx0[i] * h
				<< " x3 [" << i << "] " << grid2.x1[i] + grid2.dx1[i] * h
				<< " h " << h << endl ;
		}*/
#endif
		return ;
	}

    if (call_boundary)
	{
		z.decision |= (unsigned short) 1 << 
					  zoid<N_RANK>::NUM_BITS_DECISION - 1 ;
        l_dt_stop = dt_recursive_boundary_;
	}
    else
	{
        l_dt_stop = dt_recursive_;
	}
	bool divide_and_conquer = false ;
	double projected_time1 = 0, necessary_time = 0 ;
	double time_cut_elapsed_time = 0, space_cut_elapsed_time = ULONG_MAX ; 
	double time_cut_ptime = 0, space_cut_ptime = 0 ;
	double time_cut_rtime = 0, space_cut_rtime = 0 ;
	if (lt > l_dt_stop)  //time cut
	{
		divide_and_conquer = true ;
		//m_zoids [index].resize_children(max (2, total_num_subzoids)) ;
		m_zoids [index].set_capacity(max (2, total_num_subzoids)) ;
		m_zoids [index].resize_children(2) ;
		//decision = 1 ;
	}
	clock_gettime(CLOCK_MONOTONIC, &start1) ;
	if (lt > l_dt_stop)  //time cut
	{
		double time1 = 0, time2 = 0 ;
        // cut into time 
        int halflt = lt / 2;
        l_son_grid = l_father_grid;
    	for (int i = N_RANK-1; i >= 0; --i) {
        	touch_boundary(i, lt, l_father_grid) ;
    	}
        if (call_boundary) {
            symbolic_trap_space_time_cut_boundary(t0, t0+halflt, l_son_grid,
					index, 0, time_cut_rtime, time_cut_ptime , f, bf, time1);
        } else {
            symbolic_trap_space_time_cut_interior(t0, t0+halflt, l_son_grid,
					index, 0, time_cut_rtime, time_cut_ptime, f, time1);
        }

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = l_father_grid.dx0[i];
            l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = l_father_grid.dx1[i];
        }
        if (call_boundary) {
            symbolic_trap_space_time_cut_boundary(t0+halflt, t1, l_son_grid,
					index, 1, time_cut_rtime, time_cut_ptime, f, bf, time2);
        } else {
            symbolic_trap_space_time_cut_interior(t0+halflt, t1, l_son_grid,
					index, 1, time_cut_rtime, time_cut_ptime, f, time2);
        }
		time1 = max(time1, time2) ;
		max_loop_time = max(time1, max_loop_time) ;
    }
	clock_gettime(CLOCK_MONOTONIC, &end1) ;
	time_cut_elapsed_time = tdiff2(&end1, &start1) - time_cut_rtime ;
	assert (time_cut_elapsed_time >= 0.) ;
	assert (time_cut_ptime >= 0.) ;
	assert (time_cut_rtime >= 0.) ;
#ifndef NDEBUG
	if (lt > l_dt_stop)  //time cut
	{	
		m_zoids [index].ttime = time_cut_elapsed_time + time_cut_ptime ;
	}
#endif
	zoid_type bak ;
	if (sim_can_cut)
	{
		assert (decision) ;
		divide_and_conquer = true ;
		if (lt > l_dt_stop)
		{
			//back up the time cut children data
			bak = m_zoids [index] ;
			assert (bak.num_children == 2) ;
			//bak.resize_children(2) ;
		}
		//m_zoids [index].resize_children(total_num_subzoids) ;
		m_zoids [index].set_capacity(total_num_subzoids) ;
	}
    if (sim_can_cut) 
	{
		zoid_type bak2 ;
		int num_cases = 1 << N_RANK ;
		int best_case = 0, num_children_best_case = 1 ;
		for (int i = num_cases - 1 ; i > 0 ; i--)
		{
			int invalid_case = 0 ;
			int num_children = 1 ;
			for (int j = 0 ; j < N_RANK && ! invalid_case ; j++)
			{
				int like_to_cut_dim_j = i & 1 << j ;
				int can_cut_dim_j = decision & 1 << j + 1 ;
				//if we like to cut dim j but cannot cut dim j,
				//the case is invalid.
				invalid_case = like_to_cut_dim_j && ! can_cut_dim_j ;
				//find # of subzoids in dim j
				if (like_to_cut_dim_j && can_cut_dim_j)
				{
					num_children *= 3 ;
				}
			}
			if (invalid_case)
			{
				//invalid case.
				continue ;
			}
			assert (num_children <= total_num_subzoids) ;
			m_zoids [index].resize_children(num_children) ;
			clock_gettime(CLOCK_MONOTONIC, &start1);
			/* cut into space */
			double time = 0, rtime = 0, ptime = 0, elapsed_time = 0  ;
			if (call_boundary) 
			{
				symbolic_trap_space_cut_boundary(t0, t1, l_father_grid, 
					index, f, bf, num_subzoids, rtime, ptime, time, i) ;
			}
			else
			{
				symbolic_trap_space_cut_interior(t0, t1, l_father_grid, 
					index, f, num_subzoids, rtime, ptime, time, i) ;
			}
			//decision = 2 ;
			clock_gettime(CLOCK_MONOTONIC, &end1);
			max_loop_time = max(time, max_loop_time) ;
			elapsed_time = tdiff2(&end1, &start1) - rtime ;
			assert (elapsed_time >= 0.) ;
			assert (ptime >= 0.) ;
			assert (rtime >= 0.) ;
			if (elapsed_time + ptime < space_cut_elapsed_time + space_cut_ptime)
			{
				space_cut_elapsed_time = elapsed_time ;
				space_cut_ptime = ptime ;
				best_case = i ;
				//back up the zoid with its children.
				bak2 = m_zoids [index] ;
				assert (m_zoids [index].num_children == num_children) ;
				assert (m_zoids [index].num_children <= total_num_subzoids) ;
				//to do : update num_children with the correct # of children.
				num_children_best_case = num_children ;
			}
		}
		assert (space_cut_elapsed_time >= 0.) ;
		assert (space_cut_ptime >= 0.) ;
		assert (space_cut_rtime >= 0.) ;
		//set the decision with the best case found
		for (int j = 0 ; j < N_RANK ; j++)
		{
			int bit = best_case & 1 << j ;
			decision = decision & ~(1 << j + 1) | (bit != 0) << j + 1 ;
#ifndef NDEBUG
			int decision_bit = decision & 1 << j + 1 ;
			assert (decision_bit == bit << 1) ;
#endif
		}
		//restore the back up.
		m_zoids [index] = bak2 ;
		//m_zoids [index].resize_children(num_children) ;
		assert (m_zoids [index].num_children == num_children_best_case) ;
		assert (m_zoids [index].num_children <= total_num_subzoids) ;
	}

#ifndef NDEBUG
	if (sim_can_cut)
	{
		m_zoids [index].stime = space_cut_elapsed_time + space_cut_ptime ;
	}
#endif
	
	if (lt > l_dt_stop && sim_can_cut)
	{
		if (space_cut_elapsed_time + space_cut_ptime < time_cut_elapsed_time +
															time_cut_ptime)
		{
			//space cut is better
			projected_time1 = space_cut_ptime ;
			necessary_time = space_cut_elapsed_time ;
			//decision is already set in the initial loop above for space cut.
		}
		else
		{
			//time cut is better
			projected_time1 = time_cut_ptime ;
			necessary_time = time_cut_elapsed_time ;
			decision = 1 ;
			m_zoids [index] = bak ; //restore the backup
			assert (m_zoids [index].num_children == 2) ;
		}
	}
	else if (lt > l_dt_stop)
	{
		//time cut is the only choice
		projected_time1 = time_cut_ptime ;
		necessary_time = time_cut_elapsed_time ;
		decision = 1 ;
		assert (m_zoids [index].num_children == 2) ;
		//cout << "choosing time cut for zoid " << m_zoids [index].id << endl ;
	}
	else if (sim_can_cut)
	{
		//space cut is the only choice
		projected_time1 = space_cut_ptime ;
		necessary_time = space_cut_elapsed_time ;
		//decision is already set in the initial loop above for space cut.
	}
	
	//flush_cache() ;
	// base case
	//suppose loop_time(z) >= loop_time(z'), z' \in tree(z)
	//if divide_and_conquer_time(z) < max_{z' \in tree(z)} loop_time(z')
	//	then avoid computing loop_time(z).
	//else compute loop_time(z).
	double loop_time = 0. ;
	double loop_time_with_penalty = 0. ;
	if (divide_and_conquer && necessary_time + projected_time1 < max_loop_time)
	{
		//do not compute loop_time.
		m_zoids [index].decision |= decision ;
		projected_time += projected_time1 ;
		m_zoids [index].time = necessary_time + projected_time1 ;
	}
	else 
	{
		assert (! divide_and_conquer || 
		(divide_and_conquer && necessary_time+projected_time1 >=max_loop_time));
		//determine the looping time on the zoid
		clock_gettime(CLOCK_MONOTONIC, &start1) ;
		if (call_boundary)
		{
			base_case_kernel_boundary(t0, t1, l_father_grid, bf);
		} 
		else 
		{ 
			f(t0, t1, l_father_grid);
		}
		clock_gettime(CLOCK_MONOTONIC, &end1) ;
		loop_time = tdiff2(&end1, &start1) ;
#ifndef NDEBUG
		m_zoids [index].ltime = loop_time ;
#endif
		max_loop_time = max(loop_time, max_loop_time) ;
		
		//store the decision for the zoid and pass the redundant time 
		//to the parent
		if (divide_and_conquer && 
			necessary_time + projected_time1 < zoid_type::FUZZ * loop_time)
		{
			m_zoids [index].decision |= decision ;
			projected_time += projected_time1 ;
			m_zoids [index].time = necessary_time + projected_time1 ;
		}
		else
		{
			//m_zoids [index].decision = 0 ;
			necessary_time = loop_time ;
			m_zoids [index].time = loop_time ;
		}
	}
	m_zoids [index].max_loop_time = max_loop_time ;
	clock_gettime(CLOCK_MONOTONIC, &end) ;
	double total_time = tdiff2(&end, &start) ;
	redundant_time += total_time - necessary_time ;
}

// trap space cuts. 
template <int N_RANK> template <typename F>
inline void auto_tune<N_RANK>::trap_space_cut_interior(
				int t0, int t1, grid_info<N_RANK> const & grid, 
				simple_zoid_type * projection_zoid, F const & f)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];
	const int lt = t1 - t0 ;

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }

	int child_index = 0 ;
	assert (projection_zoid) ;
    // set up the initial grid 
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                // spawn all the grids in circular_queue_[curr_dep][] 
#if USE_CILK_FOR 
                // use cilk_for to spawn all the sub-grid 
// #pragma cilk_grainsize = 1
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    // assert all the sub-grid has done N_RANK spatial cuts 
                    assert(l_son->level == -1);
					//assert(child_index < projection_zoid->num_children) ;
					unsigned long index = projection_zoid->children [child_index] ;
					simple_zoid_type * child = &(m_simple_zoids [index]) ;
					//zoid_type * child = &(m_zoids [index]) ;
					child_index++ ; //looks like a race
                    trap_space_time_cut_interior(l_son->t0, 
						l_son->t1, l_son->grid, child, f);
                } // end cilk_for 
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                // use cilk_spawn to spawn all the sub-grid 
                pop_queue(curr_dep_pointer);
				//assert(child_index < projection_zoid->num_children) ;
				unsigned long index = projection_zoid->children [child_index] ;
				simple_zoid_type * child = &(m_simple_zoids [index]) ;
				//zoid_type * child = &(m_zoids [index]) ;
				child_index++ ;
                if (queue_len_[curr_dep_pointer] == 0)
				{
                    trap_space_time_cut_interior(l_father->t0,
 							l_father->t1, l_father->grid, child, f);
				}
                else
				{
                    cilk_spawn trap_space_time_cut_interior(
						l_father->t0, l_father->t1, l_father->grid, child, f) ;
				}
#endif
            } else {
                // performing a space cut on dimension 'level' 
                pop_queue(curr_dep_pointer);
                const grid_info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int level = l_father->level;
                const int thres = slope_[level] * lt;
                const int lt = (t1 - t0);
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                /*const bool cut_lb = (lb < tb);
				const bool can_cut = CAN_CUT_I ;*/
				//const bool can_cut = (cut_lb ? (lb >= 2 * thres) : 
				//								(tb >= 2 * thres)) ;
				//if (! can_cut) {
				if ((projection_zoid->decision & 1 << level + 1) == 0) {
                    // if we can't cut into this dimension, just directly push 
                    // it into the circular queue 
                    //
                    push_queue(curr_dep_pointer, level-1, t0, t1, 
							l_father_grid) ;
                } else  {
                    // can_cut! 
					assert ((projection_zoid->decision & 1 << level + 1) != 0) ;
                    //if (cut_lb) 
					if (projection_zoid->decision & 1 << level + 1 + N_RANK) {
                        const int mid = (lb/2);
                        grid_info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        // push the middle triangular minizoid (gray) into 
                        // circular queue of (curr_dep) 
                        //
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = slope_[level];
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -slope_[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        // cilk_sync 
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        // push the left big trapezoid (black)
                        // into circular queue of (curr_dep + 1)
                        //
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = slope_[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        // push the right big trapezoid (black)
                        // into circular queue of (curr_dep + 1)
                        //
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -slope_[level];
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                    } // end if (cut_lb) 
                    else {
                        // cut_tb 
                        const int mid = (tb/2);
                        grid_info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);
                        const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);

                        // push left black sub-grid into circular queue of (curr_dep) 
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = -slope_[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        // push right black sub-grid into circular queue of (curr_dep) 
                        l_son_grid.x0[level] = ul_start + mid;;
                        l_son_grid.dx0[level] = slope_[level];
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        // cilk_sync 
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        // push the middle gray triangular minizoid into 
                        // circular queue of (curr_dep + 1)
                        //
                        l_son_grid.x0[level] = ul_start + mid;
                        l_son_grid.dx0[level] = -slope_[level];
                        l_son_grid.x1[level] = ul_start + mid;
                        l_son_grid.dx1[level] = slope_[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } // end else (cut_tb) 
                } // end if (can_cut) 
            } // end if (performing a space cut) 
        } // end while (queue_len_[curr_dep] > 0) 
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } // end for (curr_dep < N_RANK+1) 
}

template <int N_RANK> template <typename F, typename BF>
inline void auto_tune<N_RANK>::trap_space_cut_boundary(
	int t0, int t1,	grid_info<N_RANK> const & grid, 
	simple_zoid_type * projection_zoid, F const & f, BF const & bf)
{
    queue_info *l_father;
    queue_info circular_queue_[2][ALGOR_QUEUE_SIZE];
    int queue_head_[2], queue_tail_[2], queue_len_[2];
    const int lt = t1 - t0 ;

    for (int i = 0; i < 2; ++i) {
        queue_head_[i] = queue_tail_[i] = queue_len_[i] = 0;
    }
	
	int child_index = 0 ;
	assert (projection_zoid) ;
    // set up the initial grid 
    push_queue(0, N_RANK-1, t0, t1, grid);
    for (int curr_dep = 0; curr_dep < N_RANK+1; ++curr_dep) {
        const int curr_dep_pointer = (curr_dep & 0x1);
        while (queue_len_[curr_dep_pointer] > 0) {
            top_queue(curr_dep_pointer, l_father);
            if (l_father->level < 0) {
                // spawn all the grids in circular_queue_[curr_dep][] 
#if USE_CILK_FOR 
                // use cilk_for to spawn all the sub-grid 
// #pragma cilk_grainsize = 1
				
                cilk_for (int j = 0; j < queue_len_[curr_dep_pointer]; ++j) {
                    int i = pmod((queue_head_[curr_dep_pointer]+j), ALGOR_QUEUE_SIZE);
                    queue_info * l_son = &(circular_queue_[curr_dep_pointer][i]);
                    // assert all the sub-grid has done N_RANK spatial cuts 
                    //assert(l_son->level == -1);
					//assert(child_index < projection_zoid->num_children) ;
					unsigned long index = projection_zoid->children [child_index] ;
					simple_zoid_type * child = &(m_simple_zoids [index]) ;
					//zoid_type * child = &(m_zoids [index]) ;
					child_index++ ;
                    trap_space_time_cut_boundary(l_son->t0, 
						l_son->t1, l_son->grid, child, f, bf);
                } // end cilk_for 
                queue_head_[curr_dep_pointer] = queue_tail_[curr_dep_pointer] = 0;
                queue_len_[curr_dep_pointer] = 0;
#else
                // use cilk_spawn to spawn all the sub-grid 
                pop_queue(curr_dep_pointer);
				//assert(child_index < projection_zoid->num_children) ;
				unsigned long index = projection_zoid->children [child_index] ;
				simple_zoid_type * child = &(m_simple_zoids [index]) ;
				//zoid_type * child = &(m_zoids [index]) ;
				child_index++ ;
                if (queue_len_[curr_dep_pointer] == 0) {
                    trap_space_time_cut_boundary(l_father->t0,
						l_father->t1, l_father->grid, child, f, bf);
                } else {
                    cilk_spawn trap_space_time_cut_boundary(
						l_father->t0, l_father->t1, l_father->grid, child, f, bf);
                }
#endif
            } else {
                // performing a space cut on dimension 'level' 
                pop_queue(curr_dep_pointer);
                grid_info<N_RANK> l_father_grid = l_father->grid;
                const int t0 = l_father->t0, t1 = l_father->t1;
                const int level = l_father->level;
                const int thres = slope_[level] * lt;
                const int lt = (t1 - t0);
                const int lb = (l_father_grid.x1[level] - l_father_grid.x0[level]);
                const int tb = (l_father_grid.x1[level] + l_father_grid.dx1[level] * lt - l_father_grid.x0[level] - l_father_grid.dx0[level] * lt);
                /*const bool cut_lb = (lb < tb);
//#define touch_boundary m_algo.touch_boundary
                const bool l_touch_boundary = touch_boundary(level, lt, l_father_grid);
//#undef touch_boundary
				const bool can_cut = CAN_CUT_B ;*/
				//const bool can_cut = (cut_lb ? (lb >= 2 * thres) : 
				//							  (tb >= 2 * thres)) ;
                //if (! can_cut) {
				if ((projection_zoid->decision & 1 << level + 1) == 0) {
                //if (num_zoids [level] == 0) {
                    // if we can't cut into this dimension, just directly push
                    // it into the circular queue
                    //
                    push_queue(curr_dep_pointer, level-1, t0, t1, 
							l_father_grid) ;
                } else {
					assert ((projection_zoid->decision & 1 << level + 1) != 0) ;
                    // can_cut 
                    //if (cut_lb) 
					if (projection_zoid->decision & 1 << level + 1 + N_RANK) {
                        // if cutting lb, there's no initial cut! 
                        assert(lb != phys_length_[level] || l_father_grid.dx0[level] != 0 || l_father_grid.dx1[level] != 0);
                        const int mid = lb/2;
                        grid_info<N_RANK> l_son_grid = l_father_grid;
                        const int l_start = (l_father_grid.x0[level]);
                        const int l_end = (l_father_grid.x1[level]);

                        // push the middle gray minizoid
                        // into circular queue of (curr_dep) 
                        //
                        l_son_grid.x0[level] = l_start + mid - thres;
                        l_son_grid.dx0[level] = slope_[level];
                        l_son_grid.x1[level] = l_start + mid + thres;
                        l_son_grid.dx1[level] = -slope_[level];
                        push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                        // cilk_sync 
                        const int next_dep_pointer = (curr_dep + 1) & 0x1;
                        // push one sub-grid into circular queue of (curr_dep + 1)
                        l_son_grid.x0[level] = l_start;
                        l_son_grid.dx0[level] = l_father_grid.dx0[level];
                        l_son_grid.x1[level] = l_start + mid - thres;
                        l_son_grid.dx1[level] = slope_[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);

                        // push one sub-grid into circular queue of (curr_dep + 1)
                        l_son_grid.x0[level] = l_start + mid + thres;
                        l_son_grid.dx0[level] = -slope_[level];
                        l_son_grid.x1[level] = l_end;
                        l_son_grid.dx1[level] = l_father_grid.dx1[level];
                        push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                    } // end if (cut_lb) 
                    else { // cut_tb 
                        //if (lb == phys_length_[level] && l_father_grid.dx0[level] == 0 && l_father_grid.dx1[level] == 0) { /* initial cut on the dimension */
						if (projection_zoid->decision & 
									1 << level + 1 + 3 * N_RANK) {
						   /* initial cut on the dimension */
                            assert(l_father_grid.dx0[level] == 0);
                            assert(l_father_grid.dx1[level] == 0);
                            const int mid = tb/2;
							const int dx = slope_ [level] * lt ;
                            grid_info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);

                            l_son_grid.x0[level] = l_start ;
                            l_son_grid.dx0[level] = slope_[level];
                            l_son_grid.x1[level] = l_end ;
                            l_son_grid.dx1[level] = -slope_[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);
							
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            // push triangle into circular queue of (curr_dep + 1)
                            l_son_grid.x0[level] = l_end ;
                            l_son_grid.dx0[level] = -slope_[level];
                            l_son_grid.x1[level] = l_end ;
                            l_son_grid.dx1[level] = slope_[level];
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
							/*
                            //draw a triangle with a vertex at midpoint of 
							//top base.
                            l_son_grid.x0[level] = mid - dx ;
                            l_son_grid.dx0[level] = slope_[level];
                            l_son_grid.x1[level] = mid + dx ;
                            l_son_grid.dx1[level] = -slope_[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            // cilk_sync 
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            // push trapezoid into circular queue of (curr_dep + 1)
                            l_son_grid.x0[level] = mid + dx ;
                            l_son_grid.dx0[level] = -slope_[level];
                            l_son_grid.x1[level] = l_end + mid - dx ;
                            l_son_grid.dx1[level] = slope_[level];
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
							*/
                        } else { /* NOT the initial cut! */
                            const int mid = tb/2;
                            grid_info<N_RANK> l_son_grid = l_father_grid;
                            const int l_start = (l_father_grid.x0[level]);
                            const int l_end = (l_father_grid.x1[level]);
                            const int ul_start = (l_father_grid.x0[level] + l_father_grid.dx0[level] * lt);
                            // push one sub-grid into circular queue of (curr_dep) 
                            l_son_grid.x0[level] = l_start;
                            l_son_grid.dx0[level] = l_father_grid.dx0[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = -slope_[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            // push one sub-grid into circular queue of (curr_dep) 
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = slope_[level];
                            l_son_grid.x1[level] = l_end;
                            l_son_grid.dx1[level] = l_father_grid.dx1[level];
                            push_queue(curr_dep_pointer, level-1, t0, t1, l_son_grid);

                            // cilk_sync 
                            const int next_dep_pointer = (curr_dep + 1) & 0x1;
                            // push one sub-grid into circular queue of (curr_dep + 1)
                            l_son_grid.x0[level] = ul_start + mid;
                            l_son_grid.dx0[level] = -slope_[level];
                            l_son_grid.x1[level] = ul_start + mid;
                            l_son_grid.dx1[level] = slope_[level];
                            push_queue(next_dep_pointer, level-1, t0, t1, l_son_grid);
                        }                    
                    } // end if (cut_tb) 
                } // end if (can_cut) 
			} // end if (performing a space cut) 
        } // end while (queue_len_[curr_dep] > 0) 
#if !USE_CILK_FOR
        cilk_sync;
#endif
        assert(queue_len_[curr_dep_pointer] == 0);
    } // end for (curr_dep < N_RANK+1) 
}


//continue from here.
//test if this works with the current header file.

template <int N_RANK> template <typename F>
inline void auto_tune<N_RANK>::
trap_space_time_cut_interior(int t0,
			int t1, grid_info<N_RANK> const & grid, 
			simple_zoid_type * projection_zoid, F const & f)
{
    const int lt = t1 - t0;
    grid_info<N_RANK> l_son_grid;

	assert (projection_zoid) ;
	//assert (projection_zoid->height == lt) ;
#ifndef NDEBUG
    for (int i = N_RANK-1; i >= 0; --i) {
		grid_info <N_RANK> & grid2 = projection_zoid->info ;
		int x0 = grid.x0 [i] ;
		int x1 = grid.x1 [i] ;

		int x0_ = grid2.x0 [i] ;
		int x1_ = grid2.x1 [i] ;

		int x2 = grid.x0[i] + grid.dx0[i] * lt ;
		int x3 = grid.x1[i] + grid.dx1[i] * lt ;

		int x2_ = grid2.x0[i] + grid2.dx0[i] * lt ;
		int x3_ = grid2.x1[i] + grid2.dx1[i] * lt ;

		//if (x0 != x0_ || x1 != x1_ || x2 != x2_ || x3 != x3_)
		if (x1 - x0 != x1_ - x0_ || x3 - x2 != x3_ - x2_)
		{
			cout << "zoid and proj zoid differ " << endl ;
			for (int i = N_RANK - 1 ; i >= 0 ; i--)
			{
				cout << " x0 [" << i << "] " << grid.x0 [i] 
				 << " x1 [" << i << "] " << grid.x1 [i] 
				<< " x2 [" << i << "] " << grid.x0[i] + grid.dx0[i] * lt
				<< " x3 [" << i << "] " << grid.x1[i] + grid.dx1[i] * lt
				<< " lt " << lt << endl ;
				cout << " x0 [" << i << "] " << grid2.x0 [i]
				 << " x1 [" << i << "] " << grid2.x1 [i]
				<< " x2 [" << i << "] " << grid2.x0[i] + grid2.dx0[i] * lt
				<< " x3 [" << i << "] " << grid2.x1[i] + grid2.dx1[i] * lt
				<< " lt " << lt << endl ;
			}
			assert(0) ;
		}
	}
#endif
    //if (projection_zoid->decision & 2) 
    if (projection_zoid->decision & m_space_cut_mask) 
	{
        /* cut into space */
        return trap_space_cut_interior(t0, t1, grid, projection_zoid, f) ;
    } 
	else if (projection_zoid->decision & 1)
	{
        /* cut into time */
		assert (projection_zoid->children [0]) ;
		assert (projection_zoid->children [1]) ;
        assert(lt > dt_recursive_);
        int halflt = lt / 2;
        l_son_grid = grid;
		unsigned long index = projection_zoid->children [0] ;
        trap_space_time_cut_interior(t0, t0+halflt, 
									l_son_grid, &(m_simple_zoids [index]), f);

        for (int i = 0; i < N_RANK; ++i) {
            l_son_grid.x0[i] = grid.x0[i] + grid.dx0[i] * halflt;
            l_son_grid.dx0[i] = grid.dx0[i];
            l_son_grid.x1[i] = grid.x1[i] + grid.dx1[i] * halflt;
            l_son_grid.dx1[i] = grid.dx1[i];
        }
		index = projection_zoid->children [1] ;
        return trap_space_time_cut_interior(t0+halflt, t1, 
							l_son_grid, &(m_simple_zoids [index]), f);
    }
	else
	{
		assert (projection_zoid->decision == 0) ;
		//loop
		f(t0, t1, grid);
		return ;
	}
}

template <int N_RANK> template <typename F, typename BF>
inline void auto_tune<N_RANK>::
trap_space_time_cut_boundary(int t0,
				int t1,	grid_info<N_RANK> const & grid, 
				simple_zoid_type * projection_zoid, F const & f, BF const & bf)
{
    const int lt = t1 - t0;
    grid_info<N_RANK> l_father_grid = grid, l_son_grid;

	assert (projection_zoid) ;
	//assert (projection_zoid->height == lt) ;

#ifndef NDEBUG
    for (int i = N_RANK-1; i >= 0; --i) {
		grid_info <N_RANK> & grid2 = projection_zoid->info ;
		int x0 = grid.x0 [i] ;
		int x1 = grid.x1 [i] ;

		int x0_ = grid2.x0 [i] ;
		int x1_ = grid2.x1 [i] ;

		int x2 = grid.x0[i] + grid.dx0[i] * lt ;
		int x3 = grid.x1[i] + grid.dx1[i] * lt ;

		int x2_ = grid2.x0[i] + grid2.dx0[i] * lt ;
		int x3_ = grid2.x1[i] + grid2.dx1[i] * lt ;

		//if (x0 != x0_ || x1 != x1_ || x2 != x2_ || x3 != x3_)
		if (x1 - x0 != x1_ - x0_ || x3 - x2 != x3_ - x2_)
		{
			cout << "zoid and proj zoid differ " << endl ;
			//cout << "proj zoid id " << projection_zoid->id << endl ;
			for (int i = N_RANK - 1 ; i >= 0 ; i--)
			{
				cout << " x0 [" << i << "] " << grid.x0 [i] 
				 << " x1 [" << i << "] " << grid.x1 [i] 
				<< " x2 [" << i << "] " << grid.x0[i] + grid.dx0[i] * lt
				<< " x3 [" << i << "] " << grid.x1[i] + grid.dx1[i] * lt
				<< " lt " << lt << endl ;
			}
			cout << endl ;
			for (int i = N_RANK - 1 ; i >= 0 ; i--)
			{
				cout << " x0 [" << i << "] " << grid2.x0 [i]
				 << " x1 [" << i << "] " << grid2.x1 [i]
				<< " x2 [" << i << "] " << grid2.x0[i] + grid2.dx0[i] * lt
				<< " x3 [" << i << "] " << grid2.x1[i] + grid2.dx1[i] * lt
				<< " lt " << lt << endl ;
			}

			cout << "decision " << projection_zoid->decision << endl ;
			assert(0) ;
		}
	}
#endif
	unsigned short cb = 0 ;
    for (int i = N_RANK-1; i >= 0; --i) {
        bool l_touch_boundary = touch_boundary(i, lt, l_father_grid) ;
		//cout << " l_touch_boundary " << l_touch_boundary << endl ;
		cb |= l_touch_boundary ;
    }
	unsigned short call_boundary = projection_zoid->decision >> 
					zoid<N_RANK>::NUM_BITS_DECISION - 1 ;
	assert (cb == call_boundary) ;
	
	//if (projection_zoid->decision & 2)
	if (projection_zoid->decision & m_space_cut_mask)
	{
		//cout << "space cut " << endl ;
		//cut into space 
		if (call_boundary) 
		{
			return trap_space_cut_boundary(t0, t1, l_father_grid, 
										projection_zoid, f, bf) ;
		}
		else
		{
			return trap_space_cut_interior(t0, t1, l_father_grid, 
										projection_zoid, f) ;
		}
	} 
	else if (projection_zoid->decision & 1)
	{
		//cout << "time cut " << endl ;
		// cut into time 
		assert (projection_zoid->children [0]) ;
		assert (projection_zoid->children [1]) ;
		
		int halflt = lt / 2;
		l_son_grid = l_father_grid;
		unsigned long index = projection_zoid->children [0] ;
		if (call_boundary) {
			trap_space_time_cut_boundary(t0, t0+halflt, 
								l_son_grid, &(m_simple_zoids [index]), f, bf);
		} else {
			trap_space_time_cut_interior(t0, t0+halflt, 
								l_son_grid, &(m_simple_zoids [index]), f);
		}

		for (int i = 0; i < N_RANK; ++i) {
			l_son_grid.x0[i] = l_father_grid.x0[i] + l_father_grid.dx0[i] * halflt;
			l_son_grid.dx0[i] = l_father_grid.dx0[i];
			l_son_grid.x1[i] = l_father_grid.x1[i] + l_father_grid.dx1[i] * halflt;
			l_son_grid.dx1[i] = l_father_grid.dx1[i];
		}
		index = projection_zoid->children [1] ;
		if (call_boundary) {
			return trap_space_time_cut_boundary(t0+halflt, t1, 
								l_son_grid, &(m_simple_zoids [index]), f, bf);
		} else {
			return trap_space_time_cut_interior(t0+halflt, t1, 
								l_son_grid, &(m_simple_zoids [index]), f);
		}
	}
	else
	{
		//loop
		if (call_boundary) {
			assert (projection_zoid->decision == 
					1 << zoid<N_RANK>::NUM_BITS_DECISION - 1) ;
            base_case_kernel_boundary(t0, t1, l_father_grid, bf);
        } else { 
			assert (projection_zoid->decision == 0) ;
            f(t0, t1, l_father_grid);
        }
	}
}

#undef dx_recursive_boundary_  
#undef dx_recursive_ 
#undef dt_recursive_boundary_ 
#undef dt_recursive_ 
#undef slope_ 
#undef touch_boundary 
#undef base_case_kernel_boundary
#undef base_case_kernel_boundary_rectangle

#endif