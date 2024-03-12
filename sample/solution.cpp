#ifndef __PROGTEST__
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <utility>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <compare>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"
using namespace std;
#endif /* __PROGTEST__ */

struct AProblemPackWrapper
{
    AProblemPack m_Pack;
    size_t m_CompanyId;
    atomic<size_t> toBeSolved;

    AProblemPackWrapper(AProblemPack pack, size_t companyId)
        : m_Pack(std::move(pack)), m_CompanyId(companyId), toBeSolved(m_Pack->m_ProblemsMin.size() + m_Pack->m_ProblemsCnt.size()){}

    [[nodiscard]] bool isSolved() const {return toBeSolved == 0;}
};
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
struct ACompanyWrapper
{
    explicit ACompanyWrapper(ACompany company) :
    m_Company(std::move(company)), g_QueueMtx(make_shared<mutex>()), m_EmptyQueue(make_shared<condition_variable>()) {}
    ACompany m_Company;
    shared_ptr<mutex> g_QueueMtx;
    queue<AProblemPackWrapper*> m_Queue;
    shared_ptr<condition_variable> m_EmptyQueue;
};
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
struct SolvedPackCounter
{
    explicit SolvedPackCounter(AProblemPackWrapper * mPack) : m_Pack(mPack) {}
    AProblemPackWrapper * m_Pack;
    size_t m_Counter = 0;

    ~SolvedPackCounter(){}
};
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Solver
{
    explicit Solver(AProgtestSolver solver) : m_Solver(std::move(solver)) {}
    AProgtestSolver m_Solver;
    vector<SolvedPackCounter> m_solved;
};
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
class COptimizer
{
  public:
    static bool usingProgtestSolver (){ return true;}
    static void checkAlgorithmMin (APolygon p){}
    static void checkAlgorithmCnt (APolygon p){}

    void start (int threadCount);
    void stop ();
    void addCompany (ACompany company);

    void workThread (int threadId);
    void problemReceiver (ACompanyWrapper * company, int id);
    void problemSubmitter (ACompanyWrapper * company, int id);
    void initSolvers();

    void fillSolver(AProblemPackWrapper * pack);
    void finilizeSolvers();

    bool checkFullMinSolver();
    bool checkFullCntSolver();

private:
    vector<thread>  m_WorkThreads;
    vector<thread>  m_Receivers;
    vector<thread>  m_Submitters;

    vector<ACompanyWrapper> m_Companies;

    queue<Solver*> m_ToSolve;

    Solver * m_CntSolver;
    Solver * m_MinSolver;

    atomic<size_t> m_LiveReceivers;

    mutex g_MtxToSolve;
    mutex g_MtxMinSolver;
    mutex g_MtxCntSolver;

    binary_semaphore m_ReceiversDone{0};

    condition_variable m_ToSolveEmpty;
};

void COptimizer::initSolvers()
{
    m_CntSolver = new Solver(createProgtestCntSolver());
    m_MinSolver = new Solver(createProgtestMinSolver());
}


void COptimizer::workThread (int threadId)
{
    while(true)
    {
        unique_lock<mutex> workReady(g_MtxToSolve);
        m_ToSolveEmpty.wait(workReady, [this] () {return !m_ToSolve.empty();});

        auto solver = m_ToSolve.front(); m_ToSolve.pop();
        workReady.unlock();

        if(!solver) break;

        solver->m_Solver->solve();
        for(auto & solved : solver->m_solved)
        {
            solved.m_Pack->toBeSolved -= solved.m_Counter;

            if(!solved.m_Pack->toBeSolved){
                auto id = solved.m_Pack->m_CompanyId;
                m_Companies[id].m_EmptyQueue->notify_one();
            }
        }
        delete solver;
    }
}

void COptimizer::problemReceiver (ACompanyWrapper * company, int id)
{
    while(true)
    {
        auto pack = company->m_Company->waitForPack();
        auto packWrap = pack ? new AProblemPackWrapper(pack, id) : nullptr;

        company->g_QueueMtx->lock();
        company->m_Queue.emplace(packWrap);
        company->m_EmptyQueue->notify_one();
        company->g_QueueMtx->unlock();

        if(!pack) break;
        fillSolver(packWrap);
    }

    m_LiveReceivers--;
    if(!m_LiveReceivers) m_ReceiversDone.release();
}

void COptimizer::problemSubmitter (ACompanyWrapper * company, int id)
{
    while(true)
    {
        unique_lock<mutex> queueLock (*company->g_QueueMtx);
        company->m_EmptyQueue->wait(queueLock,
                                    [&] () {return !company->m_Queue.empty() && (company->m_Queue.front() == nullptr ||
                                            company->m_Queue.front()->isSolved());});

        auto solved = company->m_Queue.front(); company->m_Queue.pop();
        queueLock.unlock();

        if(!solved) break;
        company->m_Company->solvedPack(solved->m_Pack);

        delete solved;
    }
    printf("Submitter ended\n");
}

bool COptimizer::checkFullMinSolver()
{
    if(!m_MinSolver->m_Solver->hasFreeCapacity()){
        g_MtxToSolve.lock();
        m_ToSolve.emplace(m_MinSolver);
        m_MinSolver = new Solver(createProgtestMinSolver());
        g_MtxToSolve.unlock();
        return false;
    }
    return true;
}

bool COptimizer::checkFullCntSolver()
{
    if(!m_CntSolver->m_Solver->hasFreeCapacity()){
        g_MtxToSolve.lock();
        m_ToSolve.emplace(m_CntSolver);
        m_CntSolver = new Solver(createProgtestCntSolver());
        g_MtxToSolve.unlock();
        return false;
    }
    return true;
}

void COptimizer::fillSolver(AProblemPackWrapper * pack)
{
    g_MtxMinSolver.lock();
    m_MinSolver->m_solved.emplace_back(pack);

    for(auto & p : pack->m_Pack->m_ProblemsMin){
        if(!checkFullMinSolver()) m_MinSolver->m_solved.emplace_back(pack);

        m_MinSolver->m_Solver->addPolygon(p);
        m_MinSolver->m_solved.back().m_Counter++;
    }
    g_MtxMinSolver.unlock();


    g_MtxCntSolver.lock();
    m_CntSolver->m_solved.emplace_back(pack);

    for(auto & p : pack->m_Pack->m_ProblemsCnt)
    {
        if(!checkFullCntSolver()) m_CntSolver->m_solved.emplace_back(pack);
        m_CntSolver->m_Solver->addPolygon(p);
        m_CntSolver->m_solved.back().m_Counter++;
    }
    g_MtxCntSolver.unlock();
}

void COptimizer::finilizeSolvers()
{
    g_MtxMinSolver.lock();
    if(!m_MinSolver->m_solved.empty())
    {
        g_MtxToSolve.lock();
        m_ToSolve.emplace(m_MinSolver);
        m_MinSolver = nullptr;
        g_MtxToSolve.unlock();
    }
    g_MtxMinSolver.unlock();


    g_MtxCntSolver.lock();
    if(!m_CntSolver->m_solved.empty())
    {
        g_MtxToSolve.lock();
        m_ToSolve.emplace(m_CntSolver);
        m_CntSolver = nullptr;
        g_MtxToSolve.unlock();
    }
    g_MtxCntSolver.unlock();
}



void COptimizer::start ( int threadCount )
{
    initSolvers();
    m_LiveReceivers = m_Companies.size();

    for(size_t i = 0; i < m_Companies.size(); ++i){
        m_Receivers.emplace_back(&COptimizer::problemReceiver, this, &m_Companies[i], i);
        m_Submitters.emplace_back(&COptimizer::problemSubmitter, this, &m_Companies[i], i);
    }

    for (int i = 0; i < threadCount; ++i)
        m_WorkThreads.emplace_back(&COptimizer::workThread, this, i);
}

void COptimizer::stop ()
{
    m_ReceiversDone.acquire();
    size_t live_workers = m_WorkThreads.size();

    finilizeSolvers();

    // suicide signal for workers
    for(size_t i = 0; i < live_workers; ++i){
        g_MtxToSolve.lock();
        m_ToSolve.emplace(nullptr);
        m_ToSolveEmpty.notify_all();
        g_MtxToSolve.unlock();
    }

    // suicide signal for submitters
    for(auto company : m_Companies){
        company.g_QueueMtx->lock();
        company.m_Queue.emplace(nullptr);
        company.m_EmptyQueue->notify_one();
        company.g_QueueMtx->unlock();
    }

    for(auto & th : m_Receivers) th.join();
    for(auto & th : m_WorkThreads) th.join();
    for(auto & th : m_Submitters) th.join();
}

void COptimizer::addCompany ( ACompany company )
{
    m_Companies.emplace_back(std::move(company));
}
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__
int main ()
{
  COptimizer optimizer;
  ACompanyTest  company = std::make_shared<CCompanyTest> ();

  optimizer . addCompany ( company );

  optimizer . start ( 1 );
  optimizer . stop  ();
  if ( ! company -> allProcessed () )
    throw std::logic_error ( "(some) problems were not correctly processsed" );
  return 0;
}
#endif /* __PROGTEST__ */
