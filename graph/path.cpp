//Copyright 2015 Ryan Wick

//This file is part of Bandage

//Bandage is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.

//Bandage is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.

//You should have received a copy of the GNU General Public License
//along with Bandage.  If not, see <http://www.gnu.org/licenses/>.


#include "path.h"
#include "debruijnnode.h"
#include "debruijnedge.h"
#include "../blast/blasthit.h"
#include "../blast/blastquery.h"
#include <QRegularExpression>
#include "assemblygraph.h"
#include <QStringList>


//These will try to produce a path using the given nodes.
//They will only succeed if the nodes produce one and only one path.
//If they are disconnected, branching or ambiguous, they will fail
//and only contruct an empty Path.
Path Path::makeFromUnorderedNodes(QList<DeBruijnNode *> nodes,
                                  bool strandSpecific)
{
    Path path;
    path.buildUnambiguousPathFromNodes(nodes, strandSpecific);
    return path;
}

Path Path::makeFromUnorderedNodes(std::vector<DeBruijnNode *> nodes,
                                  bool strandSpecific)
{
    QList<DeBruijnNode *> nodesList;
    for (size_t i = 0; i < nodes.size(); ++i)
        nodesList.push_back(nodes[i]);

    Path path;
    path.buildUnambiguousPathFromNodes(nodesList, strandSpecific);
    return path;
}


//This will build a Path from an ordered list of nodes.  If the nodes
//form a valid path (i.e. there is an edge connecting each step along
//the way), a Path is made, otherwise just an empty Path is made.
//This function needs exact, strand-specific nodes.  If circular is
//given, then it will also look for an edge connecting the last node
//to the first.
Path Path::makeFromOrderedNodes(QList<DeBruijnNode *> nodes, bool circular)
{
    Path path;

    path.m_nodes = nodes;

    int targetNumberOfEdges = path.m_nodes.size() - 1;
    if (circular)
        ++targetNumberOfEdges;

    for (int i = 0; i < targetNumberOfEdges; ++i)
    {
        int firstNodeIndex = i;
        int secondNodeIndex = i + 1;
        if (secondNodeIndex >= path.m_nodes.size())
            secondNodeIndex = 0;

        DeBruijnNode * node1 = path.m_nodes[firstNodeIndex];
        DeBruijnNode * node2 = path.m_nodes[secondNodeIndex];

        bool foundEdge = false;
        for (size_t j = 0; j < node1->m_edges.size(); ++j)
        {
            DeBruijnEdge * edge = node1->m_edges[j];
            if (edge->m_startingNode == node1 && edge->m_endingNode == node2)
            {
                path.m_edges.push_back(edge);
                foundEdge = true;
                break;
            }
        }

        //If we failed to find an edge connecting the nodes, then
        //the path failed.
        if (!foundEdge)
        {
            path.m_nodes.clear();
            path.m_edges.clear();
            return path;
        }
    }

    if (path.m_nodes.empty())
        return path;

    //If the code got here, then the path building was successful.
    path.m_startLocation = GraphLocation::startOfNode(path.m_nodes.front());
    path.m_endLocation = GraphLocation::endOfNode(path.m_nodes.back());

    return path;
}



Path Path::makeFromString(QString pathString, bool circular,
                          QString * pathStringFailure)
{
    Path path;

    QRegularExpression re("^(?:\\(([0-9]+)\\) ?)*((?:[^,]+[-\\+], ?)*[^,]+[-\\+])(?: ?\\(([0-9]+)\\))*$");
    QRegularExpressionMatch match = re.match(pathString);

    //If the string failed to match the regex, return an empty path.
    if (!match.hasMatch())
    {
        *pathStringFailure = "the text is not formatted correctly";
        return path;
    }

    QString startPosString = match.captured(1);
    QString nodeListString = match.captured(2);
    QString endPosString = match.captured(3);

    //Circular paths cannot have start and end positions.
    if (circular && (startPosString != "" || endPosString != ""))
    {
        *pathStringFailure = "circular paths cannot contain start or end positions";
        return path;
    }

    //Make sure there is at least one proposed node name listed.
    QStringList nodeNameList = nodeListString.simplified().split(",", QString::SkipEmptyParts);
    if (nodeNameList.empty())
    {
        *pathStringFailure = "the text is not formatted correctly";
        return path;
    }

    //Find which node names are and are not actually in the graph. 
    QList<DeBruijnNode *> nodesInGraph;
    QStringList nodesNotInGraph;
    for (int i = 0; i < nodeNameList.size(); ++i)
    {
        QString nodeName = nodeNameList[i].simplified();
        if (g_assemblyGraph->m_deBruijnGraphNodes.contains(nodeName))
            nodesInGraph.push_back(g_assemblyGraph->m_deBruijnGraphNodes[nodeName]);
        else
            nodesNotInGraph.push_back(nodeName);
    }

    //If the path contains nodes not in the graph, we fail.
    if (nodesNotInGraph.size() > 0)
    {
        *pathStringFailure = "the following nodes are not in the graph: ";
        for (int i = 0; i < nodesNotInGraph.size(); ++i)
        {
            *pathStringFailure += nodesNotInGraph[i];
            if (i != nodesNotInGraph.size() - 1)
                *pathStringFailure += ", ";
        }
        return path;
    }

    //If the code got here, then the list at least consists of valid nodes.
    //We now use it to create a Path object.
    path = Path::makeFromOrderedNodes(nodesInGraph, circular);

    //If the path is empty, then we don't have to worry about start/end
    //positions and we just return it.
    if (path.isEmpty())
    {
        if (circular)
            *pathStringFailure = "the nodes do not form a circular path";
        else
            *pathStringFailure = "the nodes do not form a path";
        return path;
    }

    //If the code got here, then a path was made, and now we must check whether
    //the start/end points are valid.
    DeBruijnNode * firstNode = path.m_nodes.front();
    DeBruijnNode * lastNode = path.m_nodes.back();

    if (startPosString.length() > 0)
    {
        int startPos = startPosString.toInt();
        if (startPos < 1 || startPos > firstNode->getLength())
        {
            *pathStringFailure = "starting node position not valid";
            return Path();
        }

        path.m_startLocation = GraphLocation(firstNode, startPos);
    }
    else
        path.m_startLocation = GraphLocation::startOfNode(firstNode);


    if (endPosString.length() > 0)
    {
        int endPos = endPosString.toInt();
        if (endPos < 1 || endPos > lastNode->getLength())
        {
            *pathStringFailure = "ending node position not valid";
            return Path();
        }

        path.m_endLocation = GraphLocation(lastNode, endPos);
    }
    else
        path.m_endLocation = GraphLocation::endOfNode(lastNode);

    return path;
}



void Path::buildUnambiguousPathFromNodes(QList<DeBruijnNode *> nodes,
                                         bool strandSpecific)
{
    if (nodes.isEmpty())
        return;

    //Loop through the nodes, trying to add them to the Path.  If node can be
    //added, then we fail and make an empty Path.  If one can be added, we quit
    //the loop and try again with the remaining nodes.
    while (nodes.size() > 0)
    {
        bool addSuccess = false;
        for (int i = 0; i < nodes.size(); ++i)
        {
            addSuccess = addNode(nodes.at(i), strandSpecific);
            if (addSuccess)
            {
                nodes.removeAt(i);
                break;
            }
        }

        if (!addSuccess)
        {
            m_nodes.clear();
            m_edges.clear();
            return;
        }
    }

    //If the nodes in the path contain other edges which connect them to each
    //other, then the path is ambiguous and we fail.
    if (checkForOtherEdges())
    {
        m_nodes.clear();
        m_edges.clear();
        return;
    }

    if (m_nodes.empty())
        return;

    //If the code got here, then the path building was successful.
    m_startLocation = GraphLocation::startOfNode(m_nodes.front());
    m_endLocation = GraphLocation::endOfNode(m_nodes.back());
}


//This function will try to add a node to the path on either end.
//It will only succeed (and return true) if there is a single way
//to add the node on one of the path's ends.
//It can, however, add a node that connects the end to both ends,
//making a circular Path.
bool Path::addNode(DeBruijnNode * newNode, bool strandSpecific)
{
    //If the Path is empty, then this function always succeeds.
    if (m_nodes.isEmpty())
    {
        m_nodes.push_back(newNode);
        return true;
    }

    //If the Path is circular, then this function fails, as there
    //is no way to add a node to a circular sequence without making
    //it ambiguous.
    if (isCircular())
        return false;

    //Check to see if the node can be added anywhere in the middle
    //of the Path.  If so, this function fails.
    for (int i = 1; i < m_nodes.size() - 1; ++i)
    {
        DeBruijnNode * middleNode = m_nodes.at(i);
        if (middleNode->isNodeConnected(newNode))
            return false;
    }

    DeBruijnNode * firstNode = m_nodes.front();
    DeBruijnNode * lastNode = m_nodes.back();

    DeBruijnEdge * edgeIntoFirst = firstNode->doesNodeLeadIn(newNode);
    DeBruijnEdge * edgeAwayFromLast = lastNode->doesNodeLeadAway(newNode);

    //If not strand-specific, then we also check to see if the reverse
    //complement of the new node can be added.
    DeBruijnEdge * revCompEdgeIntoFirst = 0;
    DeBruijnEdge * revCompEdgeAwayFromLast = 0;
    if (!strandSpecific)
    {
        revCompEdgeIntoFirst = firstNode->doesNodeLeadIn(newNode->m_reverseComplement);
        revCompEdgeAwayFromLast = lastNode->doesNodeLeadAway(newNode->m_reverseComplement);
    }

    //To be successful, either:
    // 1) exactly one of the four edge pointers should be non-null.  This
    //    indicates the node extends a linear path.
    // 2) there is both an edge away from the last and an edge into the first.
    //    This indicates that the node completes a circular Path.
    if (edgeIntoFirst == 0 && edgeAwayFromLast == 0 &&
            revCompEdgeIntoFirst == 0 && revCompEdgeAwayFromLast == 0)
        return false;

    if (edgeIntoFirst != 0 && edgeAwayFromLast == 0 &&
            revCompEdgeIntoFirst == 0 && revCompEdgeAwayFromLast == 0)
    {
        m_nodes.push_front(newNode);
        m_edges.push_front(edgeIntoFirst);
        return true;
    }

    if (edgeIntoFirst == 0 && edgeAwayFromLast != 0 &&
            revCompEdgeIntoFirst == 0 && revCompEdgeAwayFromLast == 0)
    {
        m_nodes.push_back(newNode);
        m_edges.push_back(edgeAwayFromLast);
        return true;
    }

    if (edgeIntoFirst == 0 && edgeAwayFromLast == 0 &&
            revCompEdgeIntoFirst != 0 && revCompEdgeAwayFromLast == 0)
    {
        m_nodes.push_front(newNode->m_reverseComplement);
        m_edges.push_front(revCompEdgeIntoFirst);
        return true;
    }

    if (edgeIntoFirst == 0 && edgeAwayFromLast == 0 &&
            revCompEdgeIntoFirst == 0 && revCompEdgeAwayFromLast != 0)
    {
        m_nodes.push_back(newNode->m_reverseComplement);
        m_edges.push_back(revCompEdgeAwayFromLast);
        return true;
    }

    if (edgeIntoFirst != 0 && edgeAwayFromLast != 0 &&
            revCompEdgeIntoFirst == 0 && revCompEdgeAwayFromLast == 0)
    {
        m_edges.push_back(edgeAwayFromLast);
        m_nodes.push_back(newNode);
        m_edges.push_back(edgeIntoFirst);
        return true;
    }

    if (edgeIntoFirst == 0 && edgeAwayFromLast == 0 &&
            revCompEdgeIntoFirst != 0 && revCompEdgeAwayFromLast != 0)
    {
        m_edges.push_back(revCompEdgeAwayFromLast);
        m_nodes.push_back(newNode->m_reverseComplement);
        m_edges.push_back(revCompEdgeIntoFirst);
        return true;
    }

    //If the code got here, then there are multiple ways of adding the node, so
    //we fail.
    return false;
}


//This function looks to see if there are other edges connecting path nodes
//that aren't in the list of path edges.  If so, it returns true.
//This is used to check whether a Path is ambiguous or node.
bool Path::checkForOtherEdges()
{
    //First compile a list of all edges which connect any
    //node in the Path to any other node in the Path.
    QList<DeBruijnEdge *> allConnectingEdges;
    for (int i = 0; i < m_nodes.size(); ++i)
    {
        DeBruijnNode * startingNode = m_nodes[i];
        for (int j = 0; j < m_nodes.size(); ++j)
        {
            DeBruijnNode * endingNode = m_nodes[j];
            for (size_t k = 0; k < startingNode->m_edges.size(); ++k)
            {
                DeBruijnEdge * edge = startingNode->m_edges[k];
                if (edge->m_startingNode == startingNode &&
                        edge->m_endingNode == endingNode)
                    allConnectingEdges.push_back(edge);
            }
        }
    }

    //If this list of edges is greater than the number of edges in the path,
    //then other edges exist.
    return allConnectingEdges.size() > m_edges.size();
}




//This function extracts the sequence for the whole path.  It uses the overlap
//value in the edges to remove sequences that are duplicated at the end of one
//node and the start of the next.
QByteArray Path::getPathSequence() const
{
    if (m_nodes.empty())
        return "";

    QByteArray sequence;
    QByteArray firstNodeSequence = m_nodes[0]->m_sequence;

    //If the path is circular, we trim the overlap from the first node so it is
    //flush with the end.  If the path is linear, we include the whole first
    //node.
    if (isCircular())
    {
        int overlap = m_edges.back()->m_overlap;
        int rightChars = firstNodeSequence.length() - overlap;
        if (rightChars > 0)
            sequence += firstNodeSequence.right(rightChars);
    }

    //If the path is linear, then we begin either with the entire first node
    //sequence or part of it.
    else
    {
        int rightChars = firstNodeSequence.length() - m_startLocation.getPosition() + 1;
        sequence += firstNodeSequence.right(rightChars);
    }

    //The middle nodes are not affected by whether or not the path is circular
    //or has partial node ends.
    for (int i = 1; i < m_nodes.size(); ++i)
    {
        int overlap = m_edges[i-1]->m_overlap;
        QByteArray nodeSequence = m_nodes[i]->m_sequence;
        int rightChars = nodeSequence.length() - overlap;
        if (rightChars > 0)
            sequence += nodeSequence.right(rightChars);
    }

    DeBruijnNode * lastNode = m_nodes.back();
    int amountToTrim = lastNode->m_sequence.length() - m_endLocation.getPosition();
    sequence.chop(amountToTrim);

    return sequence;
}


int Path::getLength() const
{
    int length = 0;
    for (int i = 0; i < m_nodes.size(); ++i)
        length += m_nodes[i]->getLength();

    for (int i = 0; i < m_edges.size(); ++i)
        length -= m_edges[i]->m_overlap;

    length -= m_startLocation.getPosition() - 1;

    DeBruijnNode * lastNode = m_nodes.back();
    length -= lastNode->m_sequence.length() - m_endLocation.getPosition();

    return length;
}


QString Path::getFasta() const
{
    //The description line is a comma-delimited list of the nodes in the path
    QString fasta = ">" + getString(false);

    if (isCircular())
        fasta += "(circular)";
    fasta += "\n";

    QString pathSequence = getPathSequence();
    int charactersOnLine = 0;
    for (int i = 0; i < pathSequence.length(); ++i)
    {
        fasta += pathSequence.at(i);
        ++charactersOnLine;
        if (charactersOnLine >= 70)
        {
            fasta += "\n";
            charactersOnLine = 0;
        }
    }
    fasta += "\n";

    return fasta;
}



QString Path::getString(bool spaces) const
{
    QString output;
    for (int i = 0; i < m_nodes.size(); ++i)
    {
        if (i == 0 && !m_startLocation.isAtStartOfNode())
        {
            output += "(" + QString::number(m_startLocation.getPosition()) + ")";
            if (spaces)
                output += " ";
        }

        output += m_nodes[i]->m_name;
        if (i < m_nodes.size() - 1)
        {
            output += ",";
            if (spaces)
                output += " ";
        }

        if (i == m_nodes.size() - 1 && !m_endLocation.isAtEndOfNode())
        {
            if (spaces)
                output += " ";
            output += "(" + QString::number(m_endLocation.getPosition()) + ")";
        }
    }
    return output;
}


//This function tests whether the last node in the path leads into the first.
bool Path::isCircular() const
{
    if (isEmpty())
        return false;
    if (m_edges.empty())
        return false;

    //A circular path should have the same number of nodes and edges.
    if (m_nodes.size() != m_edges.size())
        return false;

    DeBruijnEdge * lastEdge = m_edges.back();
    DeBruijnNode * firstNode = m_nodes.front();
    DeBruijnNode * lastNode = m_nodes.back();

    return (lastEdge->m_startingNode == lastNode &&
            lastEdge->m_endingNode == firstNode);
}



//These functions test whether the specified node could be added to
//the end/front of the path to form a larger valid path.
//If so, they set the path pointed to by extendedPath to equal the new, larger
//path.
bool Path::canNodeFitOnEnd(DeBruijnNode * node, Path * extendedPath) const
{
    if (isEmpty())
    {
        QList<DeBruijnNode *> nodeList;
        nodeList.push_back(node);
        *extendedPath = Path::makeFromOrderedNodes(nodeList, false);
        return true;
    }
    if (isCircular())
        return false;

    DeBruijnNode * lastNode = m_nodes.back();
    for (size_t i = 0; i < lastNode->m_edges.size(); ++i)
    {
        DeBruijnEdge * edge = lastNode->m_edges[i];
        if (edge->m_startingNode == lastNode && edge->m_endingNode == node)
        {
            *extendedPath = *this;
            extendedPath->m_edges.push_back(edge);
            extendedPath->m_nodes.push_back(node);
            extendedPath->m_endLocation = GraphLocation::endOfNode(node);
            return true;
        }
    }

    return false;
}

bool Path::canNodeFitAtStart(DeBruijnNode * node, Path * extendedPath) const
{
    if (isEmpty())
    {
        QList<DeBruijnNode *> nodeList;
        nodeList.push_back(node);
        *extendedPath = Path::makeFromOrderedNodes(nodeList, false);
        return true;
    }
    if (isCircular())
        return false;

    DeBruijnNode * firstNode = m_nodes.front();
    for (size_t i = 0; i < firstNode->m_edges.size(); ++i)
    {
        DeBruijnEdge * edge = firstNode->m_edges[i];
        if (edge->m_startingNode == node && edge->m_endingNode == firstNode)
        {
            *extendedPath = *this;
            extendedPath->m_edges.push_front(edge);
            extendedPath->m_nodes.push_front(node);
            extendedPath->m_startLocation = GraphLocation::startOfNode(node);
            return true;
        }
    }

    return false;
}


//This function builds all possible paths between the given start and end,
//within the given restrictions.
QList<Path> Path::getAllPossiblePaths(GraphLocation startLocation,
                                      GraphLocation endLocation,
                                      int nodeSearchDepth,
                                      int minDistance, int maxDistance)
{
    QList<Path> finishedPaths;
    QList<Path> unfinishedPaths;

    Path startPath;
    startPath.addNode(startLocation.getNode(), true);
    startPath.m_startLocation = startLocation;
    startPath.m_endLocation = GraphLocation::endOfNode(startLocation.getNode());
    unfinishedPaths.push_back(startPath);

    for (int i = 0; i <= nodeSearchDepth; ++i)
    {
        //Look at each of the unfinished paths to see if they end with the end
        //node.  If so, see if it has the appropriate length.
        //If it does, it will go into the final returned list.
        //If it doesn't and it's over length, then it will be removed.
        QList<Path>::iterator j = unfinishedPaths.begin();
        while (j != unfinishedPaths.end())
        {
            DeBruijnNode * lastNode = (*j).m_nodes.back();
            if (lastNode == endLocation.getNode())
            {
                Path potentialFinishedPath = *j;
                potentialFinishedPath.m_endLocation = endLocation;
                int length = potentialFinishedPath.getLength();
                if (length >= minDistance && length <= maxDistance)
                    finishedPaths.push_back(potentialFinishedPath);
                ++j;
            }
            else
            {
                if ((*j).getLength() > maxDistance)
                    j = unfinishedPaths.erase(j);
                else
                    ++j;
            }
        }

        //Make new unfinished paths by extending each of the paths.
        QList<Path> newUnfinishedPaths;
        for (int j = 0; j < unfinishedPaths.size(); ++j)
            newUnfinishedPaths.append(unfinishedPaths[j].extendPathInAllPossibleWays());
        unfinishedPaths = newUnfinishedPaths;
    }

    return finishedPaths;
}


//This function takes the current path and extends it in all possible ways by
//adding one more node, then returning a list of the new paths.  How many paths
//it returns depends on the number of edges leaving the last node in the path.
QList<Path> Path::extendPathInAllPossibleWays() const
{
    QList<Path> returnList;

    if (isEmpty())
        return returnList;

    //Since circular paths don't really have an end to extend, this function
    //doesn't work for them.
    if (isCircular())
        return returnList;

    DeBruijnNode * lastNode = m_nodes.back();
    std::vector<DeBruijnEdge *> nextEdges = lastNode->getLeavingEdges();
    for (size_t i = 0; i < nextEdges.size(); ++i)
    {
        DeBruijnEdge * nextEdge = nextEdges[i];
        DeBruijnNode * nextNode = nextEdge->m_endingNode;

        Path newPath(*this);
        newPath.m_edges.push_back(nextEdge);
        newPath.m_nodes.push_back(nextNode);
        newPath.m_endLocation = GraphLocation::endOfNode(nextNode);

        returnList.push_back(newPath);
    }

    return returnList;
}


//This function follows a path, returning the BLAST hits it finds for the given
//query.  It requires that the hits occur in order, i.e. that each hit in the
//path begins later in the query than the previous hit.
QList<BlastHit *> Path::getBlastHitsForQuery(BlastQuery * query) const
{
    QList<BlastHit *> returnList;

    BlastHit * previousHit = 0;
    for (int i = 0; i < m_nodes.size(); ++i)
    {
        DeBruijnNode * node = m_nodes[i];

        QList<BlastHit *> hitsThisNode;
        QList< QSharedPointer<BlastHit> > queryHits = query->getHits();
        for (int j = 0; j < queryHits.size(); ++j)
        {
            BlastHit * hit = queryHits[j].data();
            if (hit->m_node->m_name == node->m_name)
                hitsThisNode.push_back(hit);
        }

        std::sort(hitsThisNode.begin(), hitsThisNode.end(),
                  BlastHit::compareTwoBlastHitPointers);

        for (int j = 0; j < hitsThisNode.size(); ++j)
        {
            BlastHit * hit = hitsThisNode[j];

            //First check to make sure the hits are within the path.  This means
            //if we are in the first or last nodes of the path, we need to make
            //sure that our hit is contained within the start/end positions.
            if ( (i != 0 || hit->m_nodeStart >= m_startLocation.getPosition()) &&
                    (i != m_nodes.size()-1 || hit->m_nodeEnd <= m_endLocation.getPosition()))
            {
                //Now make sure that the hit follows the previous hit in the
                //query.
                if (previousHit == 0 ||
                        hit->m_queryStart > previousHit->m_queryStart)
                {
                    returnList.push_back(hit);
                    previousHit = hit;
                }
            }
        }
    }

    return returnList;
}


double Path::getMeanReadDepth() const
{
    long double depthTimesLengthSum = 0.0;
    int nodeLengthTotal = 0;
    for (int i = 0; i < m_nodes.size(); ++i)
    {
        DeBruijnNode * node = m_nodes[i];
        depthTimesLengthSum += node->m_coverage * node->getLength();
        nodeLengthTotal += node->getLength();
    }

    return depthTimesLengthSum / nodeLengthTotal;
}



bool Path::areIdentical(Path other) const
{
    return (m_nodes == other.m_nodes &&
            m_startLocation.areIdentical(other.m_startLocation) &&
            m_endLocation.areIdentical(other.m_endLocation));
}


bool Path::haveSameNodes(Path other) const
{
    return (m_nodes == other.m_nodes);
}



//This function checks to see if this path is actually a sub-path (i.e.
//entirely contained within) the other given path.
//It ignores start/end type and position, looking only at the nodes.
//If the two paths have the same nodes, it will return false.
bool Path::hasNodeSubset(Path other) const
{
    //To contain this path, the other path should be have a larger number of
    //nodes.
    int nodeCountDifference = other.m_nodes.size() - m_nodes.size();
    if (nodeCountDifference <= 0)
        return false;
    
    //If the paths have the same number of nodes, check to see if they are
    //identical.
    if (nodeCountDifference == 0)
        return (m_nodes == other.m_nodes);
    
    //If the code got here, then the other path has more nodes than this path.
    //We now see if we can find an ordered set of nodes in the other path that
    //matches this path's nodes.
    for (int i = 0; i <= nodeCountDifference; ++i)
    {
        QList<DeBruijnNode *> otherPathNodeSubset = other.m_nodes.mid(i, m_nodes.size());
        if (m_nodes == otherPathNodeSubset)
            return true;
    }
    
    return false;
}



void Path::extendPathToIncludeEntirityOfNodes()
{
    if (m_nodes.empty())
        return;

    m_startLocation = GraphLocation::startOfNode(m_nodes.front());
    m_endLocation = GraphLocation::endOfNode(m_nodes.back());
}


bool Path::containsNode(DeBruijnNode * node) const
{
    return m_nodes.contains(node);
}

bool Path::containsEntireNode(DeBruijnNode * node) const
{
    if (m_nodes.empty())
        return false;

    if (m_nodes.front() == node && m_startLocation.isAtStartOfNode())
        return true;

    if (m_nodes.back() == node && m_endLocation.isAtEndOfNode())
        return true;

    for (int i = 1; i < m_nodes.size() - 1; ++i)
    {
        if (m_nodes[i] == node)
            return true;
    }

    return false;
}




bool Path::isInMiddleOfPath(DeBruijnNode * node) const
{
    return containsNode(node) && !isStartingNode(node) && !isEndingNode(node);
}


//This function counts the number of times the node is in the path, not
//counting the first or last nodes.
int Path::numberOfOccurrencesInMiddleOfPath(DeBruijnNode * node) const
{
    int count = 0;
    for (int i = 1; i < m_nodes.size() - 1; ++i)
    {
        if (m_nodes[i] == node)
            ++count;
    }
    return count;
}

bool Path::isStartingNode(DeBruijnNode * node) const
{
    if (m_nodes.empty())
        return false;
    return m_nodes.front() == node;
}

bool Path::isEndingNode(DeBruijnNode * node) const
{
    if (m_nodes.empty())
        return false;
    return m_nodes.back() == node;
}


double Path::getStartFraction() const
{
    if (m_nodes.empty())
        return 0.0;

    int firstNodeLength = m_nodes.front()->getLength();
    if (firstNodeLength == 0)
        return 0.0;

    return double(m_startLocation.getPosition() - 1) / firstNodeLength;
}

double Path::getEndFraction() const
{
    if (m_nodes.empty())
        return 1.0;

    int lastNodeLength = m_nodes.back()->getLength();
    if (lastNodeLength == 0)
        return 1.0;

    return double(m_endLocation.getPosition()) / lastNodeLength;
}


int Path::getNodeCount() const
{
    return m_nodes.size();
}