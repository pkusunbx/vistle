﻿#include <limits>
#include <algorithm>
#include <boost/mpi/collectives/broadcast.hpp>
#include <core/vec.h>
#include "Tracer.h"
#include "Integrator.h"
#include "Particle.h"
#include "BlockData.h"

#ifdef TIMING
#include "TracerTimes.h"
#endif

using namespace vistle;

Particle::Particle(Index i, const Vector3 &pos, Scalar h, Scalar hmin,
      Scalar hmax, Scalar errtol, IntegrationMethod int_mode,const std::vector<std::unique_ptr<BlockData>> &bl,
      Index stepsmax, bool forward):
m_tracing(false),
m_forward(forward),
m_id(i),
m_x(pos),
m_xold(pos),
m_v(Vector3(std::numeric_limits<Scalar>::max(),0,0)), // keep large enough so that particle moves initially
m_p(0),
m_stp(0),
m_time(0),
m_dist(0),
m_block(nullptr),
m_el(InvalidIndex),
m_ingrid(true),
m_searchBlock(true),
m_integrator(h,hmin,hmax,errtol,int_mode,this, forward),
m_stopReason(StillActive),
m_useCelltree(true)
{
   m_integrator.enableCelltree(m_useCelltree);
   if (findCell(bl)) {
      m_integrator.hInit();
   }
}

Particle::~Particle(){}

Particle::StopReason Particle::stopReason() const {
    return m_stopReason;
}

void Particle::enableCelltree(bool value) {

    m_useCelltree = value;
    m_integrator.enableCelltree(value);
}

bool Particle::isActive(){

    return m_ingrid && (m_block || m_searchBlock);
}

bool Particle::inGrid(){

    return m_ingrid;
}

bool Particle::isMoving(Index maxSteps, Scalar traceLen, Scalar minSpeed) {

    if (!m_ingrid) {
       return false;
    }

    bool moving = m_v.norm() > minSpeed;
    if(m_dist > traceLen || m_stp > maxSteps || !moving){

       PointsToLines();
       if (std::abs(m_dist) > traceLen)
           this->Deactivate(DistanceLimitReached);
       else if (m_stp > maxSteps)
           this->Deactivate(StepLimitReached);
       else
           this->Deactivate(NotMoving);
       return false;
    }

    return true;
}

bool Particle::findCell(const std::vector<std::unique_ptr<BlockData>> &block) {

    if (!m_ingrid) {
        return false;
    }

    bool repeatDataFromLastBlock = false;

    if (m_block) {

        auto grid = m_block->getGrid();
        if(grid->inside(m_el, m_x)){
            return true;
        }
#ifdef TIMING
        times::celloc_start = times::start();
#endif
        m_el = grid->findCell(m_x, m_useCelltree?GridInterface::NoFlags:GridInterface::NoCelltree);
#ifdef TIMING
        times::celloc_dur += times::stop(times::celloc_start);
#endif
        if (m_el==InvalidIndex) {
            PointsToLines();
            m_searchBlock = true;
            repeatDataFromLastBlock = true;
        } else {
            m_searchBlock = false;
            return true;
        }
    }

    if (m_searchBlock) {
        m_searchBlock = false;
        for(Index i=0; i<block.size(); i++) {

            if (block[i].get() == m_block) {
                // don't try previous block again
                m_block = nullptr;
                continue;
            }
#ifdef TIMING
            times::celloc_start = times::start();
#endif
            auto grid = block[i]->getGrid();
            m_el = grid->findCell(m_x, m_useCelltree?GridInterface::NoFlags:GridInterface::NoCelltree);
#ifdef TIMING
            times::celloc_dur += times::stop(times::celloc_start);
#endif
            if (m_el!=InvalidIndex) {

                UpdateBlock(block[i].get());
                if (repeatDataFromLastBlock)
                    EmitData(m_block->m_p);
                return true;
            }
        }
        UpdateBlock(nullptr);
    }

    return false;
}

void Particle::PointsToLines(){

    if (m_block)
        m_block->addLines(m_id,m_xhist,m_vhist,m_pressures,m_steps, m_times, m_dists);

    m_xhist.clear();
    m_vhist.clear();
    m_pressures.clear();
    m_steps.clear();
    m_times.clear();
    m_dists.clear();
}

void Particle::EmitData(bool havePressure) {

   m_xhist.push_back(m_xold);
   m_vhist.push_back(m_v);
   m_steps.push_back(m_stp);
   if (havePressure)
      m_pressures.push_back(m_p); // will be ignored later on
   m_times.push_back(m_time);
   m_dists.push_back(m_dist);
}

void Particle::Deactivate(StopReason reason) {

    if (m_stopReason == StillActive)
        m_stopReason = reason;
    UpdateBlock(nullptr);
    m_ingrid = false;
}

bool Particle::Step() {

   const auto &grid = m_block->getGrid();
   auto inter = grid->getInterpolator(m_el, m_x, m_block->m_vecmap);
   m_v = inter(m_block->m_vx, m_block->m_vy, m_block->m_vz);
   if (m_block->m_p) {
      if (m_block->m_scamap != m_block->m_vecmap)
          inter = grid->getInterpolator(m_el, m_x, m_block->m_scamap);
      m_p = inter(m_block->m_p);
   }
   Scalar ddist = (m_x-m_xold).norm();
   m_xold = m_x;

   EmitData(m_block->m_p);

   if (m_forward) {
       m_time += m_integrator.h();
       m_dist += ddist;
   } else {
       m_time -= m_integrator.h();
       m_dist -= ddist;
   }

   bool ret = m_integrator.Step();
   ++m_stp;
   return ret;
}

void Particle::setTracing(bool trace) {

    m_tracing = trace;
}

bool Particle::isTracing() const {

    return m_tracing;
}

bool Particle::trace(std::vector<std::unique_ptr<BlockData>> &blocks, vistle::Index steps_max, double trace_len, double minspeed) {
    assert(m_tracing);

    bool traced = false;
    while(isMoving(steps_max, trace_len, minspeed)
          && findCell(blocks)) {
#ifdef TIMING
        double celloc_old = times::celloc_dur;
        double integr_old = times::integr_dur;
        times::integr_start = times::start();
#endif
        Step();
#ifdef TIMING
        times::integr_dur += times::stop(times::integr_start)-(times::celloc_dur-celloc_old)-(times::integr_dur-integr_old);
#endif
        traced = true;
    }
    return traced;
}

void Particle::Communicator(boost::mpi::communicator mpi_comm, int root, bool havePressure){

    boost::mpi::broadcast(mpi_comm, *this, root);

    if (mpi_comm.rank()!=root) {

        m_searchBlock = true;
    }
}

void Particle::UpdateBlock(BlockData *block) {

    m_block = block;
    m_integrator.UpdateBlock();
}
