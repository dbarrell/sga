//-----------------------------------------------
// Copyright 2010 Wellcome Trust Sanger Institute
// Written by Jared Simpson (js18@sanger.ac.uk)
// Released under the GPL
//-----------------------------------------------
//
// SGSearch - Algorithms and data structures
// for searching a string graph
//
#include "SGSearch.h"
#include "SGSearchTree.h"
#include <queue>

// Find all the walks between pX and pY that are within maxDistance
void SGSearch::findWalks(Vertex* pX, Vertex* pY, EdgeDir initialDir,
                         int maxDistance, size_t maxQueue, SGWalkVector& outWalks)
{
    // Create the initial path nodes
    WalkQueue queue;
    initializeWalkQueue(pX, initialDir, false, queue);

    while(!queue.empty())
    {
        if(queue.size() > maxQueue)
        {
            // Give up the search if there are too many possible paths to continue
            outWalks.clear();
            return;
        }

        bool bPop = false;
        
        // Process the current walk
        // This occurs in a lower scope so we can safely use a reference
        // to the top element before popping it off without having the 
        // reference hang around.
        {
            SGWalk& currWalk = queue.front();
            Edge* pWZ = currWalk.getLastEdge(); 
            Vertex* pZ = pWZ->getEnd();
           
            // Check if we have found pY or exceeded the distance
            if(pZ == pY)
            {
                outWalks.push_back(currWalk);
                bPop = true;
            }
            else if(currWalk.getExtensionDistance() > maxDistance)
            {
                bPop = true;
            }
            else
            {
                bPop = !extendWalk(pZ, pWZ->getTransitiveDir(), currWalk, queue);
            }
        }

        if(bPop)
            queue.pop_front();
    }
}

// Search the graph for a set of walks that represent alternate
// versions of the same sequence. Theese walks are found by searching
// the graph for a set of walks that start/end at a common vertex and cover
// every vertex inbetween the start/end points. Additionally, the internal
// vertices cannot have an edge to any vertex that is not in the set of vertices
// indicated by the walks.
// If these two conditions are met, we can retain one of the walks and cleanly remove
// the others.
void SGSearch::findVariantWalks(Vertex* pX, 
                                EdgeDir initialDir, 
                                int maxDistance,
                                size_t maxWalks, 
                                SGWalkVector& outWalks)
{
    findCollapsedWalks(pX, initialDir, maxDistance, 500, outWalks);

    if(outWalks.size() <= 1 || outWalks.size() > maxWalks)
    {
        outWalks.clear();
        return;
    }

    // Validate that any of the returned walks can be removed cleanly.
    // This means that for all the internal vertices on each walk (between
    // the common endpoints) the only links are to other vertices in the set

    // Construct the set of vertex IDs
    std::set<Vertex*> completeVertexSet;
    for(size_t i = 0; i < outWalks.size(); ++i)
    {
        VertexPtrVec verts = outWalks[i].getVertices();
        for(size_t j = 0; j < verts.size(); ++j)
        {
            completeVertexSet.insert(verts[j]);
        }
    }

    // Check that each vertex in the internal nodes only has
    // edges to the vertices in the set

    bool cleanlyRemovable = true;

    // Ensure that all the vertices linked to the start vertex
    // in the specified dir are present in the set.
    EdgePtrVec epv = pX->getEdges(initialDir);
    cleanlyRemovable = checkEndpointsInSet(epv, completeVertexSet);

    // Ensure that all the vertex linked to the last vertex
    // in the incoming direction are preset
    Edge* pLastEdge = outWalks.front().getLastEdge();
    Vertex* pLastVertex = pLastEdge->getEnd();
    EdgeDir lastDir = pLastEdge->getTwinDir();
    
    epv = pLastVertex->getEdges(lastDir);

    cleanlyRemovable = cleanlyRemovable && checkEndpointsInSet(epv, completeVertexSet);

    // Check that each vertex connected to an interval vertex is also present
    for(std::set<Vertex*>::iterator iter = completeVertexSet.begin(); iter != completeVertexSet.end(); ++iter)
    {
        Vertex* pY = *iter;
        if(pY == pX || pY == pLastVertex)
            continue;
        epv = pY->getEdges();
        cleanlyRemovable = cleanlyRemovable && checkEndpointsInSet(epv, completeVertexSet);
    }

    if(!cleanlyRemovable)
    {
        outWalks.clear();
    }
    return;
}

// Check that all the endpoints of the edges in the edge pointer vector
// are members of the set
bool SGSearch::checkEndpointsInSet(EdgePtrVec& epv, std::set<Vertex*>& vertexSet)
{
    for(size_t i = 0; i < epv.size(); ++i)
    {
        if(vertexSet.find(epv[i]->getEnd()) == vertexSet.end())
            return false;
    }
    return true;
}

// Return a set of walks that all start from pX and join together at some later vertex
// If no such walk exists, an empty set is returned
void SGSearch::findCollapsedWalks(Vertex* pX, EdgeDir initialDir, 
                                  int maxDistance, size_t maxNodes, 
                                  SGWalkVector& outWalks)
{
    SGSearchTree searchTree(pX, NULL, initialDir, maxDistance, maxNodes);
    searchTree.setIndexFlag(true); // we want indexed walks

    // Iteravively perform the BFS using the search tree. After each step
    // we check if the search has collapsed to a single vertex.
    bool done = false;
    while(!done)
    {
        done = !searchTree.stepOnce();
        if(searchTree.wasSearchAborted())
            break;

        Vertex* pCollapsedVertex;
        bool isCollapsed = searchTree.hasSearchConverged(pCollapsedVertex);
        if(isCollapsed)
        {
            assert(pCollapsedVertex != NULL);
            
            // pCollapsedVertex is common between all walks.
            // Check that the extension distance for any walk is no longer than maxDistance.
            // If this is the case, we truncate all walks so they end at pCollapsedVertex and return 
            // all the non-redundant walks in outWalks
            
            VertexID iLastID = pCollapsedVertex->getID();
            
            // initial walks
            SGWalkVector walkVector;
            searchTree.buildWalksToAllLeaves(walkVector);

            // truncate and index the walks in a map
            std::map<std::string, SGWalk*> nonRedundant;
            for(size_t i = 0; i < walkVector.size(); ++i)
            {
                if(walkVector[i].getExtensionDistance() > maxDistance)
                {
                    // walk is too long
                    outWalks.clear();
                    return;
                }

                walkVector[i].truncate(iLastID);
                SGWalk* pWalk = &walkVector[i];
                nonRedundant.insert(std::make_pair(walkVector[i].pathSignature(), pWalk));
            }

            // Output the final non-redundant walks and return
            for(std::map<std::string, SGWalk*>::iterator iter = nonRedundant.begin();
                iter != nonRedundant.end(); ++iter)
            {
                outWalks.push_back(*iter->second);
            }
            return;
        }
    }

    // no collapsed walk found, return empty set
    outWalks.clear();
}

// Count the number of reads that span the junction described by edge XY
// X --------------
// Y        ------------
// Z            -----------
// W                ----------
// In this case Z spans the junction but W does not. 
int SGSearch::countSpanningCoverage(Edge* pXY, size_t maxQueue)
{
    Vertex* pX = pXY->getStart();

    SGWalk walk(pX, false);
    walk.addEdge(pXY);
    
    WalkQueue queue;
    queue.push_back(walk);

    // Create the initial queue
    SGWalkVector outWalks;

    //
    while(queue.size() > 0)
    {
        if(queue.size() > maxQueue)
        {
            // Give up the search if there are too many possible paths to continue
            return -1;
        }

        bool bPop = false;
        {
            SGWalk& currWalk = queue.front();
            Edge* pWZ = currWalk.getLastEdge(); 
            Vertex* pZ = pWZ->getEnd();

            // Calculate the length of the overlap of pZ on pX. If it is <= 0
            // pZ does not overlap pX and will be removed from the walk and the walk
            // is not processed further
            int extensionDistance = currWalk.getExtensionDistance();
            int overlap = pZ->getSeqLen() - extensionDistance;
            if(overlap <= 0)
            {
                //std::cout << "Too far: " << currWalk.getExtensionDistance() << "\n";
                bPop = true;
                currWalk.popLast();
                outWalks.push_back(currWalk);
            }
            else
            {
                // Continue walk
                bPop = !extendWalk(pZ, pWZ->getTransitiveDir(), currWalk, queue);
            }
        }

        if(bPop)
            queue.pop_front();
    }

    // The outwalks may have redundant sequences
    // Make a set to calculate the coverage by unique vertices
    std::set<VertexID> vertexSet;

    for(size_t i = 0; i < outWalks.size(); ++i)
    {
        SGWalk& walk = outWalks[i];
        for(size_t j = 0; j < walk.getNumEdges(); ++j)
        {
            vertexSet.insert(walk.getEdge(j)->getEndID());
        }
    }

    return vertexSet.size();
}

//
void SGSearch::initializeWalkQueue(Vertex* pX, EdgeDir initialDir, bool bIndexWalks, WalkQueue& queue)
{
    EdgePtrVec edges = pX->getEdges(initialDir);
    for(size_t i = 0; i < edges.size(); ++i)
    {
        Edge* pEdge = edges[i];
        assert(!pEdge->getOverlap().isContainment());

        SGWalk walk(pX, bIndexWalks);
        walk.addEdge(pEdge);
        queue.push_back(walk);
    }
}

// Extend the walk by addding the neighbors of the last vertex (pX) to the walk
// Returns true if the branch was extended
bool SGSearch::extendWalk(const Vertex* pX, EdgeDir dir, SGWalk& currWalk, WalkQueue& queue)
{
    EdgePtrVec edges = pX->getEdges(dir);

    if(edges.empty())
    {
        currWalk.setFinished(true);
        return false;
    }

    // If there are multiple extensions, create new branches and add them to the queue
    for(size_t i = 1; i < edges.size(); ++i)
    {
        SGWalk branch = currWalk; 
        Edge* pBranchEdge = edges[i];
        branch.addEdge(pBranchEdge);
        queue.push_back(branch);
    }

    // Extend the current walk with the first edge
    Edge* pFirstEdge = edges[0];
    currWalk.addEdge(pFirstEdge);
    return true;
}
