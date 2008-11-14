/*
 *  ClusterAStar.cpp
 *  hog
 *
	TODO: Move getPath implementation into Abstract class. Then, all anyone needs to do to change the behaviour of this algorithm
	is override evaluate. Could contribute this back to HOG; abstractAStar??
	
	Current setup isn't too bad though; just inherit from here and override as needed.
	
 *  Created by dharabor on 14/11/08.
 *  Copyright 2008 __MyCompanyName__. All rights reserved.
 *
 */

#include "ClusterAStar.h"
#include "ClusterNode.h"
#include "mapAbstraction.h"
#include "timer.h"
#include <sstream>

using namespace std;


bool AbstractClusterAStar::isInCorridor(node* _n)
{
	ClusterNode* n = dynamic_cast<ClusterNode*>(_n);
	if(n->getParentClusterId() != corridorClusterId) 
		return false; 
	return true; 
}

/*
	1. get the current node on the open list
	2. check if node is the goal; goto 6 if yes.
	3. evaluate each neighbour of the newly opened node
		b. if neighbour is on the closed list, skip it
		a. if neighbour is already on the open list, update weights
		c. else check if node is reachable by the agent
			i. if reachable, push node on the open list
			ii. else, skip node
	4. push node on closed list
	5. if openlist is null return failure, else, goto 1
	6. return path
*/
path* ClusterAStar::getPath(graphAbstraction *aMap, node *_from, node* _to, reservationProvider *rp)
{
	nodesExpanded=0;
	nodesTouched=0;
	peakmemory = 0;
	searchtime =0;
	
	ClusterNode *from = dynamic_cast<ClusterNode*>(_from);
	ClusterNode *to = dynamic_cast<ClusterNode*>(_to);
		
	setGraphAbstraction(aMap);
		
	if(!from || !to)
		return NULL;

	if(from->getUniqueID() == to->getUniqueID())
		return NULL;
		
	if(from->getLabelL(kFirstData) == to->getLabelL(kFirstData) && from->getLabelL(kFirstData+1) == to->getLabelL(kFirstData+1))
		return NULL;
		
	if(useCorridor)
	{
		if(from->getParentClusterId() != to->getParentClusterId())
		{
			stringstream ss;
			ss << "ClusterAStar failed; useCorridor set but start/from nodes have different clusterIds. ";
			ss << " from @ "<<from->getLabelL(kFirstData)<<" ," <<from->getLabelL(kFirstData+1) <<" absLevel: "<<from->getLabelL(kAbstractionLevel);
			ss << " to @ "<<to->getLabelL(kFirstData)<<" ," <<to->getLabelL(kFirstData+1) <<" absLevel: "<<from->getLabelL(kAbstractionLevel);			
			std::cerr << ss.str();
		}
		this->corridorClusterId = from->getParentClusterId();
	}
	
	
	//TODO: need a test to check that we've set the fCost value of the start node.
	// label start node cost 0 
	from->setLabelF(kTemporaryLabel, 1*aMap->h(from, to));
	from->markEdge(0);
	
	/* initialise the search params */
	graph *g = aMap->getAbstractGraph(from->getLabelL(kAbstractionLevel));
	heap* openList = new heap(30);
	CAStarUtil::NodeMap closedList;
	openList->add(from);
	path *p = NULL;
	
	Timer t;
			
/*	int fromx = from->getLabelL(kFirstData);
	int fromy = from->getLabelL(kFirstData+1);
	int tox = to->getLabelL(kFirstData);
	int toy = to->getLabelL(kFirstData+1);
*/
	
	t.startTimer();
	while(1) 
	{
		/* get the current node on the open list and check if it contains the goal */
		peakmemory = openList->size()>peakmemory?openList->size():peakmemory;
		node* current = ((node*)openList->remove()); 
		//int cx = current->getLabelL(kFirstData);
		//int cy = current->getLabelL(kFirstData+1);
		nodesExpanded++;
		if(current == to)
		{
			p = extractBestPath(g, current->getNum());
			break;
		}
		
		/* evaluate each neighbour of the newly opened node */
		edge_iterator ei = current->getEdgeIter();
		e = current->edgeIterNext(ei);
		while(e)
		{
			// TODO: fix HOG's graph stuff; nodes identified using position in array instead of uniqueid. graph should just store a hash_map
			int neighbourid = e->getFrom()==current->getNum()?e->getTo():e->getFrom();
			node* neighbour = g->getNode(neighbourid);
			/*int nx = neighbour->getLabelL(kFirstData);
			int ny = neighbour->getLabelL(kFirstData+1);
			double weight = e->getWeight();*/

			if(!closedList[neighbour->getUniqueID()]) // skip nodes we've already closed
			{
				// if a node on the openlist is reachable via this new edge, relax the edge (see cormen et al)
				if(openList->isIn(neighbour)) 
				{	
					if(evaluate(current, neighbour)) 
					{
						relaxEdge(openList, g, e, current->getNum(), neighbourid, to); 
						nodesTouched++;
					}
				}
				else
				{
					if(evaluate(current, neighbour)) 
					{
						neighbour->setLabelF(kTemporaryLabel, MAXINT); // initial fCost = inifinity
						neighbour->setKeyLabel(kTemporaryLabel); // an initial key value for prioritisation in the openlist
						neighbour->markEdge(0);  // reset any marked edges (we use marked edges to backtrack over optimal path when goal is found)
						openList->add(neighbour);
						relaxEdge(openList, g, e, current->getNum(), neighbourid, to); 
						nodesTouched++;
					}
				}
				
			}
			e = current->edgeIterNext(ei);
		}

		closedList[current->getUniqueID()] = true;
		
		/* check if there is anything left to search; fail if not */
		if(openList->empty())
			break;
	}
	searchtime = t.endTimer();
	delete openList; 
	closedList.clear();
	return p;	
}

/* evaluate()
	check if it is possible to move from the current location to an adjacent target location.
	things we look for:
		- if the traversal involves a diagonal move, is there an equivalent 2-step move using the cardinal directions?
		
		NB: we assume that an edge exists between the current and target node parameters. Could check this explicitly but HOG's 
		implementation for this stuff is expensive (iteratres over all neighbours). We also only call this from getPath which ensures that
		we only evaluate pairs of connected nodes. 
								
		Other stuff: 
			* need to move this into abstract implementation; if edge weight > 1.0 (ie. we're looking at an edge part of an abstract graph) then
*/
bool ClusterAStar::evaluate(node* current, node* target)
{
	if(!current || !target)
		return false;
				
	int tx, ty;
	tx = target->getLabelL(kFirstData);
	ty = target->getLabelL(kFirstData+1);

	if(useCorridor && !isInCorridor(target))
		return false;

	/* check if we're moving in a cardinal direction */
	tDirection dir = getDirection(current, target);
	if(dir == kStay)
		return false;
		
	if(dir == kN || dir == kS || dir == kE || dir == kW)
		return true;
		
	/* check diagonal move is equivalent to 2-step cardinal move */
	int curx = current->getLabelL(kFirstData);
	int cury = current->getLabelL(kFirstData+1);

	mapAbstraction* map = dynamic_cast<mapAbstraction*>(this->getGraphAbstraction());
	switch(dir) // nb: use of abstractannotatedmapabstraction implies edge exists between each pair of nodes
	{
		case kNW:
			if(evaluate(current, map->getNodeFromMap(curx-1,cury)) || evaluate(current, map->getNodeFromMap(curx, cury-1)))
				return true;
			break;
		case kNE: 
			if(evaluate(current, map->getNodeFromMap(curx+1,cury)) || evaluate(current, map->getNodeFromMap(curx, cury-1)))
				return true;
			break;
		case kSE:
			if(evaluate(current, map->getNodeFromMap(curx+1,cury)) || evaluate(current, map->getNodeFromMap(curx, cury+1)))
				return true;
			break;
		case kSW:
			if(evaluate(current, map->getNodeFromMap(curx-1,cury)) || evaluate(current, map->getNodeFromMap(curx, cury+1)))
				return true;
			break;
		default:
			cerr << "\nfatal: edge weight > 1 but move direction is not diagonal!\n ";
			break;
	}
	
	return false;
}

/* given two adjacent locations, the current position and a target position, figure out which of the eight compass directions the move
is equivalent to (n,ne,e,se,s,sw,w,nw) 
TODO: there's alot of duplication; this method has an equivalent addPathToCache in almost every Unit class. Should merge into a static.
*/
tDirection ClusterAStar::getDirection(node* current, node* target)
{
		int deltax = current->getLabelL(kFirstData) - target->getLabelL(kFirstData);
		int deltay = current->getLabelL(kFirstData+1) - target->getLabelL(kFirstData+1);
		
		int dir = kStay;
		switch(deltax)
		{
			case 1: // add westerly component
				dir = kW;
				break;
			case -1: // add easterly component
				dir = kE;
				break;
			case 0:
				break;
			default: // not moving along x-axis
				return kStay;
		};
		
		switch(deltay)
		{
			case 1: // add northerly component
				dir = dir|kN;
				break;
			case -1: // add southerly component
				dir = dir|kS;
				break;
			case 0:
				break;
			default: // not moving along y-axis
				return kStay;
		}
		
		return (tDirection)dir;
}

void ClusterAStar::logFinalStats(statCollection *stats)
{
	stats->addStat("nodesExpanded",getName(),getNodesExpanded());
	stats->addStat("nodesTouched",getName(),getNodesTouched());
	stats->addStat("peakMemory",getName(),getPeakMemory());
	stats->addStat("searchTime",getName(),getSearchTime());
}