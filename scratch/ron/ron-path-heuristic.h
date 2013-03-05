/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright 2007 University of Washington
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef RON_PATH_HEURISTIC_H
#define RON_PATH_HEURISTIC_H

#include "ron-peer-table.h"
#include "ns3/core-module.h"
#include "boost/function.hpp"
#include <vector>

namespace ns3 {

//TODO: enum for choosing which heuristic?

/** This class represents a heuristic for choosing overlay paths.  Derived classes must override the ComparePeers
    function to implement the actual heuristic logic. */
class RonPathHeuristic : public Object
{
public:
  static TypeId GetTypeId (void);

  virtual ~RonPathHeuristic ();

  RonPeerEntry GetNextPeer (Ptr<RonPeerEntry> destination);
  Ipv4Address GetNextPeerAddress (Ptr<RonPeerEntry> destination);
  void AddHeuristic (Ptr<RonPathHeuristic> other);
  void SetPeerTable (Ptr<RonPeerTable> table);
  void SetSourcePeer (Ptr<RonPeerEntry> peer);
  Ptr<RonPeerEntry> GetSourcePeer ();

  class NoValidPeerException : public std::exception
  {
  public:
    virtual const char* what() const throw()
    {
      return "No valid peers could be chosen for the overlay.";
    }
  };

protected:
  /** Sets the estimated likelihood of reaching the specified peer */
  void SetLikelihood (Ptr<RonPeerEntry> peer, double lh);

  Ptr<RonPeerTable> m_peers;
  UniformVariable random; //for random decisions
  Ptr<RonPeerEntry> m_source;
  std::string m_summaryName;
  std::string m_shortName;
  double m_weight;
  std::map<Ptr<RonPeerEntry>, double> m_likelihoods;
  std::list<Ptr<RonPeerEntry> > m_peersAttempted;
  std::list<Ptr<RonPathHeuristic> > m_aggregateHeuristics;

  /** Set the estimated likelihood of reaching the destination through each peer. */
  virtual void UpdateLikelihoods (Ptr<RonPeerEntry> destination) = 0;
  bool m_updatedOnce;
  void ClearLikelihoods ();
  virtual bool SameRegion (Ptr<RonPeerEntry> peer1, Ptr<RonPeerEntry> peer2);

private:
  /** May only need to assign likelihoods once.
      Set this to true to avoid clearing LHs after each newly chosen peer. */
  //boost::function<bool (RonPeerEntry peer1, RonPeerEntry peer2)> GetPeerComparator (Ptr<RonPeerEntry> destination = NULL);
};

class RandomRonPathHeuristic : public RonPathHeuristic
{
public:
  RandomRonPathHeuristic ();
  static TypeId GetTypeId (void);
private:
  virtual void UpdateLikelihoods (Ptr<RonPeerEntry> destination);
};


class OrthogonalRonPathHeuristic : public RonPathHeuristic
{
public:
  OrthogonalRonPathHeuristic ();
  static TypeId GetTypeId (void);
private:
  bool updatedOnce; //only need to assign random probs once
  virtual void UpdateLikelihoods (Ptr<RonPeerEntry> destination);
};

} //namespace
#endif //RON_PATH_HEURISTIC_H
