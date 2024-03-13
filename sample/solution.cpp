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

enum SolverType{
    MIN, CNT, END
};

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
template<typename T>
class AtomicQueue
{
public:
    explicit AtomicQueue(function<bool(queue<T> & q)> & pred) : m_Pred(std::move(pred)){}
    T pop(){
        unique_lock<mutex> lock (g_Mtx);
        m_Cond.wait(lock, [&] (){ return !m_Queue.empty() && m_Pred(m_Queue);});
        auto first = m_Queue.front(); m_Queue.pop();
        return first;
    }

    void push(T data){
        unique_lock<mutex> lock(g_Mtx);
        m_Queue.emplace(std::move(data));
        m_Cond.notify_one();
    }

    void notify() {m_Cond.notify_one();}
private:
    mutex g_Mtx;
    condition_variable m_Cond;
    queue<T> m_Queue;
    function<bool(queue<T> & q)> m_Pred;
};

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

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
    explicit ACompanyWrapper(ACompany company, function<bool(queue<AProblemPackWrapper*> &)> pred) :
    m_Company(std::move(company)), m_Queue(pred) {}
    ACompany m_Company;
    AtomicQueue<AProblemPackWrapper*> m_Queue;
};

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

struct SolvedPackCounter
{
    explicit SolvedPackCounter(AProblemPackWrapper * mPack) : m_Pack(mPack) {}
    AProblemPackWrapper * m_Pack;
    size_t m_Counter = 0;
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
    COptimizer(function<bool(queue<Solver*>&)> pred = [](queue<Solver*>&) { return true; }) : m_ToSolve(pred){};

    static bool usingProgtestSolver (){ return true;}
    static void checkAlgorithmMin (APolygon p){}
    static void checkAlgorithmCnt (APolygon p){}

    void start (int threadCount);
    void stop ();
    void addCompany (ACompany company);

    void workThread ();
    void problemReceiver (ACompanyWrapper * company, int id);
    void problemSubmitter (ACompanyWrapper * company, int id);

    void initSolvers();
    void fillSolver(AProblemPackWrapper * pack);
    void setNewSolver(SolverType type);
    void finalizeSolvers();

private:
    vector<thread>  m_WorkThreads;
    vector<thread>  m_Receivers;
    vector<thread>  m_Submitters;

    deque<ACompanyWrapper> m_Companies;
    AtomicQueue<Solver*> m_ToSolve;

    Solver * m_CntSolver;
    Solver * m_MinSolver;

    mutex g_MtxMinSolver;
    mutex g_MtxCntSolver;
};
//-------------------------------------------------------------------------------------------------------------------------------------------------------------

void COptimizer::initSolvers()
{
    m_CntSolver = new Solver(createProgtestCntSolver());
    m_MinSolver = new Solver(createProgtestMinSolver());
}


void COptimizer::workThread()
{
    while(true)
    {
        auto solver = m_ToSolve.pop();
        if(!solver) break;

        solver->m_Solver->solve();

        for(auto & solved : solver->m_solved){
            solved.m_Pack->toBeSolved -= solved.m_Counter;

            if(!solved.m_Pack->toBeSolved){
                size_t id = solved.m_Pack->m_CompanyId;
                m_Companies[id].m_Queue.notify();
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

        company->m_Queue.push(packWrap);
        if(!pack) break;
        fillSolver(packWrap);
    }
}


void COptimizer::problemSubmitter (ACompanyWrapper * company, int id)
{
    while(true)
    {
        auto solved = company->m_Queue.pop();
        if(!solved) break;

        company->m_Company->solvedPack(solved->m_Pack);
        delete solved;
    }
}


void COptimizer::setNewSolver(SolverType type)
{
    switch (type){
        case MIN:
            m_ToSolve.push(m_MinSolver);
            m_MinSolver = new Solver(createProgtestMinSolver());
            break;
        case CNT:
            m_ToSolve.push(m_CntSolver);
            m_CntSolver = new Solver(createProgtestCntSolver());
            break;
        case END:
            if(!m_MinSolver->m_solved.empty()) m_ToSolve.push(m_MinSolver);
            if(!m_CntSolver->m_solved.empty()) m_ToSolve.push(m_CntSolver);
            break;
    }
}


void COptimizer::fillSolver(AProblemPackWrapper * pack)
{
    g_MtxMinSolver.lock();
    m_MinSolver->m_solved.emplace_back(pack);

    for(auto & p : pack->m_Pack->m_ProblemsMin){
        if(!m_MinSolver->m_Solver->hasFreeCapacity()){
            setNewSolver(MIN);
            m_MinSolver->m_solved.emplace_back(pack);
        }

        m_MinSolver->m_Solver->addPolygon(p);
        m_MinSolver->m_solved.back().m_Counter++;
    }
    if(!m_MinSolver->m_Solver->hasFreeCapacity()) setNewSolver(MIN);
    g_MtxMinSolver.unlock();


    g_MtxCntSolver.lock();
    m_CntSolver->m_solved.emplace_back(pack);

    for(auto & p : pack->m_Pack->m_ProblemsCnt)
    {
        if(!m_CntSolver->m_Solver->hasFreeCapacity()){
            setNewSolver(CNT);
            m_CntSolver->m_solved.emplace_back(pack);
        }
        m_CntSolver->m_Solver->addPolygon(p);
        m_CntSolver->m_solved.back().m_Counter++;
    }
    if(!m_CntSolver->m_Solver->hasFreeCapacity()) setNewSolver(CNT);
    g_MtxCntSolver.unlock();
}


void COptimizer::finalizeSolvers()
{
    unique_lock<mutex> minLock (g_MtxMinSolver), cntLock (g_MtxCntSolver);
    setNewSolver(END);
}


void COptimizer::start ( int threadCount )
{
    initSolvers();

    for(size_t i = 0; i < m_Companies.size(); ++i){
        m_Receivers.emplace_back(&COptimizer::problemReceiver, this, &m_Companies[i], i);
        m_Submitters.emplace_back(&COptimizer::problemSubmitter, this, &m_Companies[i], i);
    }

    for (int i = 0; i < threadCount; ++i)
        m_WorkThreads.emplace_back(&COptimizer::workThread, this);
}


void COptimizer::stop ()
{
    for(auto & th : m_Receivers) th.join();
    finalizeSolvers();
    for(size_t i = 0; i < m_WorkThreads.size(); ++i) m_ToSolve.push(nullptr);
    for(auto & th : m_WorkThreads) th.join();
    for(auto & th : m_Submitters) th.join();
}


void COptimizer::addCompany ( ACompany company )
{
    std::function<bool(queue<AProblemPackWrapper*> & q)> isFirstSolved =
            [] (queue<AProblemPackWrapper*> & q) {return !q.front() || !q.front()->toBeSolved;};

    m_Companies.emplace_back(std::move(company), isFirstSolved);
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__
int main ()
{
  COptimizer optimizer;
  ACompanyTest  company = std::make_shared<CCompanyTest> ();

  optimizer . addCompany ( company );

  optimizer . start (10);
  optimizer . stop  ();
  if ( ! company -> allProcessed () )
    throw std::logic_error ( "(some) problems were not correctly processsed" );
  return 0;
}
#endif /* __PROGTEST__ */
