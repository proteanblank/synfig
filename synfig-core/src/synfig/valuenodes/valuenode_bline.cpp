/* === S Y N F I G ========================================================= */
/*!	\file valuenode_bline.cpp
**	\brief Implementation of the "BLine" valuenode conversion.
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007, 2008 Chris Moore
**  Copyright (c) 2011 Carlos López
**
**	This file is part of Synfig.
**
**	Synfig is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 2 of the License, or
**	(at your option) any later version.
**
**	Synfig is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with Synfig.  If not, see <https://www.gnu.org/licenses/>.
**	\endlegal
*/
/* ========================================================================= */

/* === H E A D E R S ======================================================= */

#ifdef USING_PCH
#	include "pch.h"
#else
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "valuenode_bline.h"
#include "valuenode_const.h"
#include "valuenode_composite.h"
#include <synfig/canvas.h>
#include <synfig/general.h>
#include <synfig/localization.h>
#include <synfig/valuenode_registry.h>
#include <synfig/exception.h>
#include <synfig/bezier.h>
#include <synfig/segment.h>
#include <synfig/curve_helper.h>
#include <algorithm> // for std::swap

#endif

/* === U S I N G =========================================================== */

using namespace synfig;

/* === M A C R O S ========================================================= */

#define EPSILON 0.0000001f

/* === G L O B A L S ======================================================= */

REGISTER_VALUENODE(ValueNode_BLine, RELEASE_VERSION_0_61_06, "bline", N_("Spline"))

/* === P R O C E D U R E S ================================================= */

inline float
linear_interpolation(const float& a, const float& b, float c)
{ return (b-a)*c+a; }

inline Vector
linear_interpolation(const Vector& a, const Vector& b, float c)
{ return (b-a)*c+a; }

inline Vector
radial_interpolation(const Vector& a, const Vector& b, float c)
{
	// if either extreme is zero then use linear interpolation instead
	if (a.is_equal_to(Vector::zero()) || b.is_equal_to(Vector::zero()))
		return linear_interpolation(a, b, c);

	affine_combo<Real,float> mag_combo;
	affine_combo<Angle,float> ang_combo;

	Real mag(mag_combo(a.mag(),b.mag(),c));
	Angle angle_a(Angle::tan(a[1],a[0]));
	Angle angle_b(Angle::tan(b[1],b[0]));
	float diff = Angle::deg(angle_b - angle_a).get();
	if (diff < -180) angle_b += Angle::deg(360);
	else if (diff > 180) angle_a += Angle::deg(360);
	Angle ang(ang_combo(angle_a, angle_b, c));

	return Point( mag*Angle::cos(ang).get(),mag*Angle::sin(ang).get() );
}

inline void
transform_coords(Vector in, Vector& out, const Point& coord_origin, const Point *coord_sys)
{
	in -= coord_origin;
	out[0] = in * coord_sys[0];
	out[1] = in * coord_sys[1];
}

inline void
untransform_coords(const Vector& in, Vector& out, const Point& coord_origin, const Point *coord_sys)
{
	out[0] = in * coord_sys[0];
	out[1] = in * coord_sys[1];
	out += coord_origin;
}

ValueBase
synfig::convert_bline_to_segment_list(const ValueBase& bline)
{
	std::vector<Segment> ret;

//	std::vector<BLinePoint> list(bline.operator std::vector<BLinePoint>());
	//std::vector<BLinePoint> list(bline);
	std::vector<BLinePoint> list(bline.get_list_of(BLinePoint()));
	std::vector<BLinePoint>::const_iterator	iter;

	BLinePoint prev,first;

	//start with prev = first and iter on the second...

	if(list.empty()) return ValueBase(ValueBase::List(ret.begin(), ret.end()),bline.get_loop());
	first = prev = list.front();

	for(iter=++list.begin();iter!=list.end();++iter)
	{
		ret.push_back(
			Segment(
				prev.get_vertex(),
				prev.get_tangent2(),
				iter->get_vertex(),
				iter->get_tangent1()
			)
		);
		prev=*iter;
	}
	if(bline.get_loop())
	{
		ret.push_back(
			Segment(
				prev.get_vertex(),
				prev.get_tangent2(),
				first.get_vertex(),
				first.get_tangent1()
			)
		);
	}
	return ValueBase(ret,bline.get_loop());
}

ValueBase
synfig::convert_bline_to_width_list(const ValueBase& bline)
{
	std::vector<Real> ret;
//	std::vector<BLinePoint> list(bline.operator std::vector<BLinePoint>());
	//std::vector<BLinePoint> list(bline);
	std::vector<BLinePoint> list(bline.get_list_of(BLinePoint()));
	std::vector<BLinePoint>::const_iterator	iter;

	if(bline.empty())
		return ValueBase(type_list);

	for(iter=list.begin();iter!=list.end();++iter)
		ret.push_back(iter->get_width());

	if(bline.get_loop())
		ret.push_back(list.front().get_width());

	return ValueBase(ValueBase::List(ret.begin(), ret.end()),bline.get_loop());
}

Real
synfig::find_closest_point(const ValueBase &bline, const Point &pos, Real radius, bool loop, Point *out_point)
{
	const Real minstep = 0.01;
	const Real maxstep = 0.1;
	if (radius==0) radius = 10000000;

	Real closest = 10000000;
	Real best_time = 0;
	int best_index = -1;
	synfig::Point best_point;

	const ValueBase::List &list = bline.get_list();
	const int size = (int)list.size();
	if (size == 0)
		return 0;
	const int count = loop ? size : size - 1;
	
	for(int i0 = 0; i0 < count; ++i0) {
		int i1 = (i0 + 1) % size;
		const BLinePoint &p0 = list[i0].get(BLinePoint());
		const BLinePoint &p1 = list[i1].get(BLinePoint());
		
		bezier<Point> curve;
		curve[0] = p0.get_vertex();
		curve[1] = curve[0] + p0.get_tangent2()/3;
		curve[3] = p1.get_vertex();
		curve[2] = curve[3] - p1.get_tangent1()/3;
		curve.sync();

		//set the step size based on the size of the picture
		Real len = (curve[1] - curve[0]).mag()
				 + (curve[2] - curve[1]).mag()
				 + (curve[3] - curve[2]).mag();
		
		// want to make the distance between lines happy
		Real step = synfig::clamp(len/(2*radius), minstep, maxstep);
		float time = 0;
		Real c = find_closest(curve, pos, step, &closest, &time);
		if(c < closest) {
			closest = c;
			best_time = time;
			best_index = i0;
			best_point = curve(best_time);
		}
	}

	if (best_index < 0) return 0;

	if (out_point) *out_point = best_point;
	return (best_index + best_time)/count;
}

Real
synfig::std_to_hom(const ValueBase &bline, Real pos, bool index_loop, bool bline_loop)
{
	const Real loops = index_loop ? floor(pos) : 0.0;
	pos -= loops;

	// trivial cases
	if (approximate_less_or_equal(pos, Real(0)))
		return loops;
	if (approximate_greater_or_equal(pos, Real(1)))
		return loops + 1;

	const std::vector<BLinePoint> list(bline.get_list_of(BLinePoint()));
	const size_t size = list.size();
	if (size == 0)
		return loops;
	const size_t count = bline_loop? size : size - 1;
	if (count < 1)
		return loops + pos;

	// Calculate the lengths and the total length
	std::vector<Real> lengths;
	const Real bline_total_length = bline_length(list, bline_loop, &lengths);
	// If the total length of the bline is zero return pos
	if (approximate_equal(bline_total_length, 0.0))
		return pos;
	const size_t from_vertex = size_t(pos*count);
	// Calculate the partial length until the bezier that holds the current
	Real partial_length = 0;
	std::vector<Real>::const_iterator length_iter(lengths.begin());
	for (size_t i = 0; i < from_vertex; ++i, ++length_iter)
		partial_length += *length_iter;
	// Calculate the remaining length of the position over current bezier
	// Setup the curve of the current bezier.
	const size_t next_vertex = (from_vertex + 1) % size;
	const BLinePoint& blinepoint0 = list[from_vertex];
	const BLinePoint& blinepoint1 = list[next_vertex];
	const hermite<Vector> curve(blinepoint0.get_vertex(), blinepoint1.get_vertex(),
							blinepoint0.get_tangent2(), blinepoint1.get_tangent1());
	// add the distance on the bezier we are on.
	partial_length += curve.find_distance(0.0, pos*count - from_vertex);
	// and return the homogeneous position
	return loops+partial_length/bline_total_length;
}

Real
synfig::hom_to_std(const ValueBase &bline, Real pos, bool index_loop, bool bline_loop)
{
	const Real loops = index_loop ? floor(pos) : 0.0;
	pos -= loops;
	assert(approximate_greater_or_equal(pos, 0.0));

	// trivial cases
	if (approximate_less_or_equal(pos, Real(0)))
		return loops;
	if (approximate_greater_or_equal(pos, Real(1)))
		return loops + 1;

	const std::vector<BLinePoint> list(bline.get_list_of(BLinePoint()));
	const size_t size = list.size();
	if (size == 0)
		return loops;
	const size_t count = bline_loop? size : size - 1;
	if (count < 1)
		return loops + pos;

	// Calculate the lengths and the total length
	std::vector<Real> lengths;
	const Real bline_total_length = bline_length(bline, bline_loop, &lengths);
	// Calculate the my partial length (the length where pos is)
	const Real target_length = pos * bline_total_length;
	std::vector<Real>::const_iterator length_iter(lengths.begin());
	// Find the previous bezier where we pos is placed and the sum
	// of lengths to it (cumulative_length)
	// also remember the bezier's length where we stop
	Real cumulative_length = 0;
	size_t from_vertex = 0;
	Real segment_length = 0;
	while (approximate_greater(target_length, cumulative_length) && length_iter != lengths.end()) {
		segment_length = *length_iter;
		cumulative_length += segment_length;

		++length_iter;
		++from_vertex;
	}
	if (approximate_equal(target_length, cumulative_length)) {
		return loops + Real(from_vertex) / count;
	} else {
		// correct the iters and partial length in case we passed over
		--length_iter;
		cumulative_length -= *length_iter;
		--from_vertex;
	}
	// set up the curve
	const BLinePoint& blinepoint0 = list[from_vertex];
	const BLinePoint& blinepoint1 = list[(from_vertex+1) % size];
	const hermite<Vector> curve(blinepoint0.get_vertex(),   blinepoint1.get_vertex(),
	                           blinepoint0.get_tangent2(), blinepoint1.get_tangent1());
	// Find the solution to which is the standard position which matches the current
	// homogeneous position
	// Secant method: http://en.wikipedia.org/wiki/Secant_method
	Real sn(0.0); // the standard position on current bezier
	Real sn1(0.0), sn2(1.0);
	const Real t0((target_length-cumulative_length)/segment_length); // the homogeneous position on the current bezier
	int iterations=0;
	const int max_iterations=100;
	const Real max_error(0.00001);
	Real error;
	Real fsn1(t0-curve.find_distance(0.0,sn1)/segment_length);
	Real fsn2(t0-curve.find_distance(0.0,sn2)/segment_length);
	do {
		sn=sn1-fsn1*((sn1-sn2)/(fsn1-fsn2));
		Real fsn=t0-curve.find_distance(0.0, sn)/segment_length;
		sn2=sn1;
		sn1=sn;
		fsn2=fsn1;
		fsn1=fsn;
		error=fabs(fsn2-fsn1);
		iterations++;
	} while (error>max_error && max_iterations > iterations);
	// convert the current standard index (s) to the bline's standard index
	// and return it
	return loops+Real(from_vertex + sn)/count;
}

Real
synfig::bline_length(const ValueBase &bline, bool bline_loop, std::vector<Real> *lengths)
{
	if (lengths)
		lengths->clear();
	const std::vector<BLinePoint> list(bline.get_list_of(BLinePoint()));
	if (list.empty())
		return 0;
	const size_t max_vertex_index = bline_loop ? list.size() : list.size() - 1;
	if (max_vertex_index < 1)
		return Real();

	if (lengths)
		lengths->reserve(max_vertex_index);

	// Calculate the lengths and the total length
	Real total_length = 0;
	for (size_t i0 = 0; i0 < max_vertex_index; ++i0) {
		const size_t i1 = (i0 + 1) % list.size();
		const BLinePoint& blinepoint0 = list[i0];
		const BLinePoint& blinepoint1 = list[i1];
		const hermite<Vector> curve(blinepoint0.get_vertex(), blinepoint1.get_vertex(),
							blinepoint0.get_tangent2(), blinepoint1.get_tangent1());
		const Real l = curve.length();
		if(lengths) lengths->push_back(l);
		total_length+=l;
	}

	return total_length;
}
/* === M E T H O D S ======================================================= */


ValueNode_BLine::ValueNode_BLine(Canvas::LooseHandle canvas):
	ValueNode_DynamicList(type_bline_point, canvas)
{
	DEBUG_LOG("SYNFIG_DEBUG_SET_PARENT_CANVAS",
		"%s:%d should have already set parent canvas for bline %p to %p (using dynamic_list constructor)\n", __FILE__, __LINE__, this, canvas.get());
}

ValueNode_BLine::~ValueNode_BLine()
{
}

ValueNode_BLine*
ValueNode_BLine::create(const ValueBase& value, Canvas::LooseHandle canvas)
{
	if(value.get_type()!=type_list)
		return 0;

	// don't set the parent canvas yet - do it just before returning from this function
	// otherwise we'll start constructing and destroying handles to the new bline before
	// we have a permanent handle to it and it will be destroyed
	ValueNode_BLine* value_node(new ValueNode_BLine());

	if(!value.empty())
	{
		Type &type(value.get_contained_type());
		if (type == type_bline_point)
		{
//			std::vector<BLinePoint> bline_points(value.operator std::vector<BLinePoint>());
			//std::vector<BLinePoint> bline_points(value);
			std::vector<BLinePoint> bline_points(value.get_list_of(BLinePoint()));
			std::vector<BLinePoint>::const_iterator iter;

			for(iter=bline_points.begin();iter!=bline_points.end();iter++)
			{
				value_node->add(ValueNode::Handle(ValueNode_Composite::create(*iter, canvas)));
			}
			value_node->set_loop(value.get_loop());
		}
		else
		if (type == type_segment)
		{
			// Here, we want to convert a list of segments
			// into a list of BLinePoints. We make an assumption
			// that the segment list is continuous(sp), but not necessarily
			// smooth.

			value_node->set_loop(false);
//			std::vector<Segment> segments(value.operator std::vector<Segment>());
//			std::vector<Segment> segments(value);
			std::vector<Segment> segments(value.get_list_of(Segment()));
			std::vector<Segment>::const_iterator iter,last(segments.end());
			--last;
			ValueNode_Const::Handle prev,first;

			for(iter=segments.begin();iter!=segments.end();iter++)
			{
#define PREV_POINT	prev->get_value().get(BLinePoint())
#define FIRST_POINT	first->get_value().get(BLinePoint())
#define CURR_POINT	curr->get_value().get(BLinePoint())
				if(iter==segments.begin())
				{
					prev = ValueNode_Const::Handle::cast_dynamic(
							ValueNode_Const::create(type_bline_point, canvas) );
					{
						BLinePoint prev_point(PREV_POINT);
						prev_point.set_vertex(iter->p1);
						prev_point.set_tangent1(iter->t1);
						prev_point.set_width(0.01);
						prev_point.set_origin(0.5);
						prev_point.set_split_tangent_both(false);
						prev->set_value(prev_point);
					}
					first=prev;
					value_node->add(ValueNode::Handle(prev));
				}

				if(iter==last && iter->p2.is_equal_to(FIRST_POINT.get_vertex()))
				{
					value_node->set_loop(true);
					if(!iter->t2.is_equal_to(FIRST_POINT.get_tangent1()))
					{
						BLinePoint first_point(FIRST_POINT);
						first_point.set_tangent1(iter->t2);
						first->set_value(first_point);
					}
					continue;
				}

				ValueNode_Const::Handle curr =
					ValueNode_Const::Handle::cast_dynamic(
						ValueNode_Const::create(type_bline_point, canvas) );
				{
					BLinePoint curr_point(CURR_POINT);
					curr_point.set_vertex(iter->p2);
					curr_point.set_tangent1(iter->t2);
					curr_point.set_width(0.01);
					curr_point.set_origin(0.5);
					curr_point.set_split_tangent_both(false);
					curr->set_value(curr_point);
				}
				if(!PREV_POINT.get_tangent1().is_equal_to(iter->t1))
				{
					BLinePoint prev_point(PREV_POINT);
					prev_point.set_split_tangent_both(true);
					prev_point.set_tangent2(iter->t1);
					prev->set_value(prev_point);
				}
				value_node->add(ValueNode::Handle(curr));
				prev=curr;
			}

		}
		else
		{
			// We got a list of who-knows-what. We don't have any idea
			// what to do with it.
			return 0;
		}
	}

	value_node->set_parent_canvas(canvas);

	return value_node;
}

ValueNode_BLine::ListEntry
ValueNode_BLine::create_list_entry(int index, Time time, Real origin)
{
	ValueNode_BLine::ListEntry ret;

	synfig::BLinePoint prev,next;
	synfig::BLinePoint bline_point;
	int prev_i,next_i;

	if(link_count())
	{
		index=index%link_count();
		assert(index>=0);
		if(!list[index].status_at_time(time))
			next_i=find_next_valid_entry(index,time);
		else
			next_i=index;
		prev_i=find_prev_valid_entry(index,time);
		next=(*list[next_i].value_node)(time).get(BLinePoint());
		prev=(*list[prev_i].value_node)(time).get(BLinePoint());
		hermite<Vector> curve(prev.get_vertex(),next.get_vertex(),prev.get_tangent2(),next.get_tangent1());
		hermite<Vector> left;
		hermite<Vector> right;
		curve.subdivide(&left, &right, origin);
		bline_point.set_vertex(left[3]);
		bline_point.set_width((next.get_width()-prev.get_width())*origin+prev.get_width());
		bline_point.set_split_tangent_radius(true);
		bline_point.set_split_tangent_angle(false);
		bline_point.set_tangent1((left[2]-left[3])*-3);
		bline_point.set_tangent2((right[1]-right[0])*3);
		bline_point.set_origin(origin);
	}
	ret.index=index;
	ret.set_parent_value_node(this);
	ret.value_node=ValueNode_Composite::create(bline_point);
	ret.value_node->set_parent_canvas(get_parent_canvas());
	return ret;
}


ValueBase
ValueNode_BLine::operator()(Time t)const
{
	DEBUG_LOG("SYNFIG_DEBUG_VALUENODE_OPERATORS",
		"%s:%d operator()\n", __FILE__, __LINE__);

	std::vector<BLinePoint> ret_list;

	std::vector<ListEntry>::const_iterator iter,first_iter;
	bool first_flag(true);
	bool rising;
	int index(0);
	float next_scale(1.0f);

	BLinePoint prev,first;
	first.set_origin(100.0f);

	// loop through all the list's entries
	for(iter=list.begin();iter!=list.end();++iter,index++)
	{
		// how 'on' is this vertex?
		float amount(iter->amount_at_time(t,&rising));

		assert(amount>=0.0f);
		assert(amount<=1.0f);

		// it's fully on
		if (amount > 1.0f - EPSILON)
		{
			if(first_flag)
			{
				first_iter=iter;
				first=prev=get_blinepoint(iter, t);
				first_flag=false;
				ret_list.push_back(first);
				continue;
			}

			BLinePoint curr;
			curr=get_blinepoint(iter, t);

			if(next_scale!=1.0f)
			{
				ret_list.back().set_split_tangent_both(true);
				ret_list.back().set_tangent2(prev.get_tangent2()*next_scale);

				ret_list.push_back(curr);

				ret_list.back().set_split_tangent_both(true);
				ret_list.back().set_tangent2(curr.get_tangent2());
				ret_list.back().set_tangent1(curr.get_tangent1()*next_scale);

				next_scale=1.0f;
			}
			else
			{
				ret_list.push_back(curr);
			}

			prev=curr;
		}
		// it's partly on
		else if(amount>0.0f)
		{
			std::vector<ListEntry>::const_iterator begin_iter,end_iter;

			// This is where the interesting stuff happens
			// We need to seek forward in the list to see what the next
			// active point is

			BLinePoint blp_here_on;  // the current vertex, when fully on
			BLinePoint blp_here_off; // the current vertex, when fully off
			BLinePoint blp_here_now; // the current vertex, right now (between on and off)
			BLinePoint blp_prev_off; // the beginning of dynamic group when fully off
			BLinePoint blp_next_off; // the end of the dynamic group when fully off

			int dist_from_begin(0), dist_from_end(0);
			Time off_time, on_time;

			if(!rising)	// if not rising, then we were fully on in the past, and will be fully off in the future
			{
				try{ on_time=iter->find_prev(t)->get_time(); }
				catch(...) { on_time=Time::begin(); }
				try{ off_time=iter->find_next(t)->get_time(); }
				catch(...) { off_time=Time::end(); }
			}
			else // otherwise we were fully off in the past, and will be fully on in the future
			{
				try{ off_time=iter->find_prev(t)->get_time(); }
				catch(...) { off_time=Time::begin(); }
				try{ on_time=iter->find_next(t)->get_time(); }
				catch(...) { on_time=Time::end(); }
			}

			blp_here_on=get_blinepoint(iter, on_time);
//			blp_here_on=(*iter->value_node)(t).get(blp_here_on);

			// Find "end" of dynamic group - ie. search forward along
			// the bline from the current point until we find a point
			// which is more 'on' than the current one
			end_iter=iter;
//			for(++end_iter;begin_iter!=list.end();++end_iter)
			for(++end_iter;end_iter!=list.end();++end_iter)
				if(end_iter->amount_at_time(t)>amount)
					break;

			// If we did not find an end of the dynamic group...
			// Writeme!  at least now it doesn't crash if first_iter
			// isn't set yet
			if(end_iter==list.end())
			{
				if(get_loop() && !first_flag)
					end_iter=first_iter;
				else
					end_iter=--list.end();
			}

			blp_next_off=get_blinepoint(end_iter, off_time);

			// Find "begin" of dynamic group
			begin_iter=iter;
			blp_prev_off.set_origin(100.0f); // set the origin to 100 (which is crazy) so that we can check to see if it was found
			do
			{
				if(begin_iter==list.begin())
				{
					if(get_loop())
						begin_iter=list.end();
					else
						break;
				}

				--begin_iter;
				dist_from_begin++;

				// if we've gone all around the loop, give up
				if(begin_iter==iter)
					break;

				if(begin_iter->amount_at_time(t)>amount)
				{
					blp_prev_off=get_blinepoint(begin_iter, off_time);
					break;
				}
			}while(true);

			// If we did not find a begin
			if(blp_prev_off.get_origin()==100.0f)
			{
				// Writeme! - this needs work, but at least now it
				// doesn't crash
				if(first_flag)
					begin_iter=list.begin();
				else
					begin_iter=first_iter;
				blp_prev_off=get_blinepoint(begin_iter, off_time);
			}

			// this is how the curve looks when we have completely vanished
			hermite<Vector> curve(blp_prev_off.get_vertex(),   blp_next_off.get_vertex(),
									   blp_prev_off.get_tangent2(), blp_next_off.get_tangent1());

			// where would we be on this curve, how wide will we be, and
			// where will our tangents point (all assuming that we hadn't vanished)
			blp_here_off.set_vertex(curve(blp_here_on.get_origin()));
			blp_here_off.set_width((blp_next_off.get_width()-blp_prev_off.get_width())*blp_here_on.get_origin()+blp_prev_off.get_width());
			blp_here_off.set_tangent1(curve.derivative(blp_here_on.get_origin()));
			blp_here_off.set_tangent2(curve.derivative(blp_here_on.get_origin()));

			float prev_tangent_scalar(1.0f);
			float next_tangent_scalar(1.0f);

			//synfig::info("index_%d:dist_from_begin=%d",index,dist_from_begin);
			//synfig::info("index_%d:dist_from_end=%d",index,dist_from_end);

			// If we are the next to the begin
			if(begin_iter==--std::vector<ListEntry>::const_iterator(iter) || dist_from_begin==1)
				prev_tangent_scalar=linear_interpolation(blp_here_on.get_origin(), 1.0f, amount);
			else
				prev_tangent_scalar=linear_interpolation(blp_here_on.get_origin()-prev.get_origin(), 1.0f, amount);

			// If we are the next to the end
			if(end_iter==++std::vector<ListEntry>::const_iterator(iter) || dist_from_end==1)
				next_tangent_scalar=linear_interpolation(1.0-blp_here_on.get_origin(), 1.0f, amount);
			else if(list.end()!=++std::vector<ListEntry>::const_iterator(iter))
			{
				BLinePoint next;
				next=get_blinepoint(++std::vector<ListEntry>::const_iterator(iter), t);
				next_tangent_scalar=linear_interpolation(next.get_origin()-blp_here_on.get_origin(), 1.0f, amount);
			}
			else
				//! \todo this isn't quite right; we should handle looped blines identically no matter where the loop happens
				//! and we currently don't.  this at least makes it a lot better than it was before
				next_tangent_scalar=linear_interpolation(blp_next_off.get_origin()-blp_here_on.get_origin(), 1.0f, amount);
			next_scale=next_tangent_scalar;

			//blp_here_now.set_vertex(linear_interpolation(blp_here_off.get_vertex(), blp_here_on.get_vertex(), amount));
			// if(false)
			// {
			// 	// My first try
			// 	Point ref_point_begin(((*begin_iter->value_node)(off_time).get(prev).get_vertex() +
			// 						   (*end_iter->value_node)(off_time).get(prev).get_vertex()) * 0.5);
			// 	Point ref_point_end(((*begin_iter->value_node)(on_time).get(prev).get_vertex() +
			// 						 (*end_iter->value_node)(on_time).get(prev).get_vertex()) * 0.5);
			// 	Point ref_point_now(((*begin_iter->value_node)(t).get(prev).get_vertex() +
			// 						 (*end_iter->value_node)(t).get(prev).get_vertex()) * 0.5);
			// 	Point ref_point_linear(linear_interpolation(ref_point_begin, ref_point_end, amount));
			//
			// 	blp_here_now.set_vertex(linear_interpolation(blp_here_off.get_vertex(), blp_here_on.get_vertex(), amount) +
			// 							(ref_point_now-ref_point_linear));
			// 	blp_here_now.set_tangent1(linear_interpolation(blp_here_off.get_tangent1(), blp_here_on.get_tangent1(), amount));
			// 	blp_here_now.set_split_tangent_both(blp_here_on.get_split_tangent_both());
			// 	if(blp_here_now.get_split_tangent_both())
			// 		blp_here_now.set_tangent2(linear_interpolation(blp_here_off.get_tangent2(), blp_here_on.get_tangent2(), amount));
			// }
			// else
			{
				// My second try

				// define 3 coordinate systems:
				Point off_coord_sys[2],   off_coord_origin; // when the current vertex is completely off
				Point on_coord_sys[2] ,    on_coord_origin; // when the current vertex is completely on
				Point curr_coord_sys[2], curr_coord_origin; // the current state - somewhere in between

				// for each of the 3 systems, the origin is half way between the previous and next active point
				// and the axes are based on a vector from the next active point to the previous
				{
					const Point   end_pos_at_off_time(get_blinepoint(end_iter,   off_time).get_vertex());
					const Point begin_pos_at_off_time(get_blinepoint(begin_iter, off_time).get_vertex());
					off_coord_origin=(begin_pos_at_off_time + end_pos_at_off_time)/2;
					off_coord_sys[0]=(begin_pos_at_off_time - end_pos_at_off_time).norm();
					off_coord_sys[1]=off_coord_sys[0].perp();

					const Point   end_pos_at_on_time(get_blinepoint(end_iter,   on_time).get_vertex());
					const Point begin_pos_at_on_time(get_blinepoint(begin_iter, on_time).get_vertex());
					on_coord_origin=(begin_pos_at_on_time + end_pos_at_on_time)/2;
					on_coord_sys[0]=(begin_pos_at_on_time - end_pos_at_on_time).norm();
					on_coord_sys[1]=on_coord_sys[0].perp();

					const Point   end_pos_at_current_time(get_blinepoint(end_iter,   t).get_vertex());
					const Point begin_pos_at_current_time(get_blinepoint(begin_iter, t).get_vertex());
					curr_coord_origin=(begin_pos_at_current_time + end_pos_at_current_time)/2;
					curr_coord_sys[0]=(begin_pos_at_current_time - end_pos_at_current_time).norm();
					curr_coord_sys[1]=curr_coord_sys[0].perp();

					// Invert (transpose) the last of these matrices, since we use it for transform back
					std::swap(curr_coord_sys[0][1],curr_coord_sys[1][0]);
				}

				/* The code that was here before used just end_iter as the origin, rather than the mid-point */

				// We know our location and tangent(s) when fully on and fully off
				// Transform each of these into their corresponding coordinate system
				Point trans_on_point, trans_off_point;
				Vector trans_on_t1, trans_on_t2, trans_off_t1, trans_off_t2;

				transform_coords(blp_here_on.get_vertex(),  trans_on_point,  on_coord_origin,  on_coord_sys);
				transform_coords(blp_here_off.get_vertex(), trans_off_point, off_coord_origin, off_coord_sys);

#define COORD_SYS_RADIAL_TAN_INTERP 1

#ifdef COORD_SYS_RADIAL_TAN_INTERP
				transform_coords(blp_here_on.get_tangent1(),  trans_on_t1,  Point::zero(), on_coord_sys);
				transform_coords(blp_here_off.get_tangent1(), trans_off_t1, Point::zero(), off_coord_sys);

				if(blp_here_on.get_split_tangent_both())
				{
					transform_coords(blp_here_on.get_tangent2(),  trans_on_t2,  Point::zero(), on_coord_sys);
					transform_coords(blp_here_off.get_tangent2(), trans_off_t2, Point::zero(), off_coord_sys);
				}
#endif

				{
					// Interpolate between the 'on' point and the 'off' point and untransform to get our point's location
					Point tmp;
					untransform_coords(linear_interpolation(trans_off_point, trans_on_point, amount),
									   tmp, curr_coord_origin, curr_coord_sys);
					blp_here_now.set_vertex(tmp);
				}

#define INTERP_FUNCTION		radial_interpolation
//#define INTERP_FUNCTION	linear_interpolation

#ifdef COORD_SYS_RADIAL_TAN_INTERP
				{
					Vector tmp;
					untransform_coords(INTERP_FUNCTION(trans_off_t1,trans_on_t1,amount), tmp, Point::zero(), curr_coord_sys);
					blp_here_now.set_tangent1(tmp);
				}
#else
				blp_here_now.set_tangent1(radial_interpolation(blp_here_off.get_tangent1(),blp_here_on.get_tangent1(),amount));
#endif

				if (blp_here_on.get_split_tangent_both())
				{
					blp_here_now.set_split_tangent_both(true);
#ifdef COORD_SYS_RADIAL_TAN_INTERP
					{
						Vector tmp;
						untransform_coords(INTERP_FUNCTION(trans_off_t2,trans_on_t2,amount), tmp, Point::zero(), curr_coord_sys);
						blp_here_now.set_tangent2(tmp);
					}
#else
					blp_here_now.set_tangent2(radial_interpolation(blp_here_off.get_tangent2(),blp_here_on.get_tangent2(),amount));
#endif
				}
				else
					blp_here_now.set_split_tangent_both(false);
			}

			blp_here_now.set_origin(blp_here_on.get_origin());
			blp_here_now.set_width(linear_interpolation(blp_here_off.get_width(), blp_here_on.get_width(), amount));

			// Handle the case where we are the first vertex
			if(first_flag)
			{
				blp_here_now.set_tangent1(blp_here_now.get_tangent1()*prev_tangent_scalar);
				first_iter=iter;
				first=prev=blp_here_now;
				first_flag=false;
				ret_list.push_back(blp_here_now);
				continue;
			}

			ret_list.back().set_split_tangent_both(true);
			ret_list.back().set_tangent2(prev.get_tangent2()*prev_tangent_scalar);
			ret_list.push_back(blp_here_now);
			ret_list.back().set_split_tangent_both(true);
			//ret_list.back().set_tangent2(blp_here_now.get_tangent1());
			ret_list.back().set_tangent1(blp_here_now.get_tangent1()*prev_tangent_scalar);

			prev=blp_here_now;
		}
	}

	if(next_scale!=1.0f)
	{
		ret_list.back().set_split_tangent_both(true);
		ret_list.back().set_tangent2(prev.get_tangent2()*next_scale);
	}

/*
	if(get_loop() && !first_flag)
	{
		ret_list.push_back(
			Segment(
			prev.get_vertex(),
			prev.get_tangent2(),
			first.get_vertex(),
			first.get_tangent1()
			)
		);
	}
*/

	if(list.empty())
		synfig::warning(std::string("ValueNode_BLine::operator()():")+_("No entries in list"));
	else
	if(ret_list.empty())
		synfig::warning(std::string("ValueNode_BLine::operator()():")+_("No entries in ret_list"));

	return ValueBase(ValueBase::List(ret_list.begin(), ret_list.end()),get_loop());
}

String
ValueNode_BLine::link_local_name(int i)const
{
	assert(i>=0 && (unsigned)i<list.size());
	return strprintf(_("Vertex %03d"),i+1);
}



LinkableValueNode*
ValueNode_BLine::create_new()const
{
	return new ValueNode_BLine();
}

bool
ValueNode_BLine::check_type(Type &type)
{
	return type==type_list;
}


BLinePoint
ValueNode_BLine::get_blinepoint(std::vector<ListEntry>::const_iterator current, Time t) const
{
	BLinePoint bpcurr((*current->value_node)(t).get(BLinePoint()));
	if(!bpcurr.get_boned_vertex_flag())
		return bpcurr;

	std::vector<ListEntry>::const_iterator next(current), previous(current); //iterators current, next, previous
	BLinePoint bpprev,bpnext; //BLinePoints next, previous
	Vector t1,t2;
	Vector tt1,tt2; // Calculated tangents
	Point v,vn,vp; // Transformed current Vertex, next Vertex, previous Vertex
	Point vs,vns,vps; // Setup current Vertex, next Vertex, previous Vertex
	Angle beta1,beta2; //Final angle of tangents (transformed)
	Angle beta01,beta02; //Original angle of tangnets (untransformed)
	Angle alpha; // Increment of angle produced in the segment next-previous
	Angle gamma; // Compensation due to the variation relative to the midpoint.

	next++;
	if(next==list.end())
		next=list.begin();
	if(current==list.begin())
		previous=list.end();
	previous--;

	bpprev=(*previous->value_node)(t).get(BLinePoint());
	bpnext=(*next->value_node)(t).get(BLinePoint());

	t1=bpcurr.get_tangent1();
	t2=bpcurr.get_tangent2();
	v=bpcurr.get_vertex();
	vp=bpprev.get_vertex();
	vn=bpnext.get_vertex();
	vs=bpcurr.get_vertex_setup();
	vps=bpprev.get_vertex_setup();
	vns=bpnext.get_vertex_setup();
	beta01=t1.angle();
	beta02=t2.angle();
	// New approach: I calculate the needed relative change of the tangents
	// in relation to the segment that joins the next and previous vertices.
	// Then add a compensation due to the modification relative to the mid point.
	// If the blinepoint tangent is not split it is not needed the compensation
	// in fact the compensation makes it worst so it makes only sense when the
	// vertex has a particular "shape" by its split tangents.
	alpha=(vn-vp).angle()-(vns-vps).angle();
	if (bpcurr.get_split_tangent_both())
		gamma=((v-(vn+vp)*0.5).angle()-(vn-vp).angle()) - ((vs-(vns+vps)*0.5).angle()-(vns-vps).angle());
	else
		gamma=Angle::zero();

	beta1=alpha + gamma + beta01;
	beta2=alpha + gamma + beta02;
	tt1[0]=t1.mag()*Angle::cos(beta1).get();
	tt1[1]=t1.mag()*Angle::sin(beta1).get();
	tt2[0]=t2.mag()*Angle::cos(beta2).get();
	tt2[1]=t2.mag()*Angle::sin(beta2).get();
	bpcurr.set_tangent1(tt1);
	bpcurr.set_tangent2(tt2);

	return bpcurr;
}

#ifdef _DEBUG
void
ValueNode_BLine::ref() const noexcept
{
	DEBUG_LOG("SYNFIG_DEBUG_BLINE_REFCOUNT",
		"%s:%d %lx   ref bline %*s -> %2d\n", __FILE__, __LINE__, uintptr_t(this), (count()*2), "", count()+1);

	LinkableValueNode::ref();
}

void
ValueNode_BLine::unref()const
{
	DEBUG_LOG("SYNFIG_DEBUG_BLINE_REFCOUNT",
		"%s:%d %lx unref bline %*s%2d <-\n", __FILE__, __LINE__, uintptr_t(this), ((count()-1)*2), "", count()-1);

	LinkableValueNode::unref();
}
#endif

LinkableValueNode::Vocab
ValueNode_BLine::get_children_vocab_vfunc()const
{
	LinkableValueNode::Vocab ret;
	for(unsigned int i=0; i<list.size();i++)
	{
		ret.push_back(ParamDesc(strprintf("item%04d",i))
			.set_local_name(strprintf(_("Vertex %03d"),i+1))
		);
	}

	return ret;
}
