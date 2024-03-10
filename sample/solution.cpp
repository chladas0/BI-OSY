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
    shared_ptr<CProblemPack> m_Pack;
    size_t m_ProblemId, m_CompanyId;
    bool m_Suicide = false;
    size_t m_MinSize, m_CntSize;

    AProblemPackWrapper(shared_ptr<CProblemPack> pack, size_t problemId, size_t companyId, bool suicide = false)
        : m_Pack(std::move(pack)), m_ProblemId(problemId), m_CompanyId(companyId), m_Suicide(suicide),
        m_MinSize(m_Pack ? m_Pack->m_ProblemsMin.size() : 0 ), m_CntSize(m_Pack ? m_Pack->m_ProblemsCnt.size() : 0)
    {}
    bool isSolved() const {return m_MinSize == 0 && m_CntSize == 0;}
};
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
struct ACompanyWrapper
{
    explicit ACompanyWrapper(ACompany comp) : company(std::move(comp)) {}
    ACompanyWrapper(const ACompanyWrapper &comp) : company(comp.company), problems(comp.problems){}

    mutex m_CompanyMutex;
    condition_variable submitNotReady;
    ACompany company;
    unordered_map<int, AProblemPackWrapper> problems;
    size_t id = 0;
    size_t lastId = 0;
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
    void problemReceiver (int companyId);
    void problemSubmitter (int companyId);

    void addToSolve(const AProblemPackWrapper& pack);
    void solveOneCnt(const APolygon& polygon);
    void solveOneMin(const APolygon& polygon);
    void initSolver(){m_MinSolvers.emplace_back(createProgtestMinSolver()),
                      m_CntSolvers.emplace_back(createProgtestCntSolver());}

private:
    vector<thread>  m_WorkThreads;
    vector<thread>  m_Receivers;
    vector<thread>  m_Submitters;

    vector<ACompanyWrapper> m_Companies;
    list<AProblemPackWrapper> m_toReview;

    size_t m_LiveWorkers;
    size_t m_LiveReceivers;

    queue<AProblemPackWrapper> m_ToSolve;

    mutex g_MtxToSolve;
    mutex g_MtxLiveReceivers;
    mutex g_MtxLiveWorkers;
    mutex g_MinSolver;
    mutex g_CntSolver;
    mutex g_ToReview;

    condition_variable allReceived;
    condition_variable workersEmpty;
    condition_variable toSolveEmpty;
    condition_variable toSubmitEmpty;

    // solver stuff
    size_t cntIdx = 0;
    size_t minIdx = 0;
    vector<AProgtestSolver> m_MinSolvers;
    vector<AProgtestSolver> m_CntSolvers;
};


// Solving -------------------------------------------------------------------------------------------------------------
void COptimizer::addToSolve(const AProblemPackWrapper& pack)
{
    g_ToReview.lock();
    m_toReview.emplace_back(pack);
    g_ToReview.unlock();

    for(auto & i : pack.m_Pack->m_ProblemsMin) solveOneMin(i);

}

void COptimizer::solveOneCnt(const APolygon& polygon)
{
}

void COptimizer::solveOneMin(const APolygon& polygon)
{
    if(!m_MinSolvers[minIdx]->addPolygon(polygon))
    {
        m_MinSolvers.emplace_back(createProgtestMinSolver());
        minIdx++;
        m_MinSolvers[minIdx]->addPolygon(polygon);

        size_t solved = m_MinSolvers[minIdx-1]->solve();

        g_ToReview.lock();
        for(auto it = m_toReview.begin(); it != m_toReview.end(); )
        {
            if(solved == 0) break;

            if(solved >= it->m_MinSize)
            {
                solved -= it->m_MinSize;
                it->m_MinSize = 0;
            }

            else{
                it->m_MinSize -= solved;
                solved = 0;
            }

            // notify the submit thread
            if(it->m_MinSize == 0){

                for(auto & x : it->m_Pack->m_ProblemsMin)
                    printf("res: %f\n", x->m_TriangMin);

                it = m_toReview.erase(it);
            }
            else it++;
        }
        g_ToReview.unlock();
    }
}





// ---------------------------------------------------------------------------------------------------------------------
void COptimizer::workThread (int threadId)
{
    while(true)
    {
        unique_lock<mutex> toSolveLock(g_MtxToSolve);

        toSolveEmpty.wait(toSolveLock, [this]{return !m_ToSolve.empty();});

        auto pack = m_ToSolve.front();
        m_ToSolve.pop();

        if(pack.m_Suicide) break;

        addToSolve(pack);
    }

    g_MtxLiveWorkers.lock();

    m_LiveWorkers--;
    if(m_LiveWorkers == 0)
        workersEmpty.notify_all();

    g_MtxLiveWorkers.unlock();
}

void COptimizer::problemReceiver (int companyId)
{
  size_t id = 0;

  while(true)
  {
    auto pack = m_Companies[companyId].company->waitForPack();

    if(pack == nullptr) break;

    AProblemPackWrapper x(pack, id, companyId);

    // debug maybe not :D HIHIHI
    for(auto & a : pack->m_ProblemsCnt)
        a->m_TriangMin = -1000000;

    // Emplace the pack to be solved
    g_MtxToSolve.lock();
    m_ToSolve.emplace(x);
    toSolveEmpty.notify_all();
    g_MtxToSolve.unlock();

    // Add it to the solved problems
    m_Companies[companyId].m_CompanyMutex.lock();
    m_Companies[companyId].problems.emplace(id, x);
    m_Companies[companyId].lastId = id + 1;
    m_Companies[companyId].m_CompanyMutex.unlock();
    id++;
  }

  // update the live receivers
  g_MtxLiveReceivers.lock();

  m_LiveReceivers--;
  if(m_LiveReceivers == 0)
        allReceived.notify_all();

  g_MtxLiveReceivers.unlock();
}

void COptimizer::problemSubmitter (int companyId)
{
//    int id = 0;
//    while(true)
//    {
//        unique_lock<mutex> ul(m_Companies[companyId].m_CompanyMutex);
//        m_Companies[companyId].submitNotReady.wait(ul,
//                                                   [this, companyId, id] {return
//                                                   m_Companies[companyId].problems.count(id) &&
//                                                   m_Companies[companyId].problems[id].isSolved();});
//
//        auto pack = m_Companies[companyId].problems[id];
//
//        if(pack.m_Suicide) break;
//
//        printf("Company %d, solved problem %ld\n", companyId, pack.m_ProblemId);
//
////        for(auto & x : pack.m_Pack->m_ProblemsCnt)
////            printf("res: %f\n", x->m_TriangMin);
//
////        m_Companies[companyId].company->solvedPack(pack.m_Pack);
////        m_Companies[companyId].problems.erase(m_Companies[companyId].problems.begin());
//        id++;
//    }
}


void COptimizer::start ( int threadCount )
{
    initSolver();
    m_LiveWorkers = threadCount;
    m_LiveReceivers = m_Companies.size();

    for(int i = 0; i < (int)m_Companies.size(); i++){
        m_Receivers.emplace_back(&COptimizer::problemReceiver, this, i);
        m_Submitters.emplace_back(&COptimizer::problemSubmitter, this, i);
    }

    for (int i = 0; i < threadCount; i++)
        m_WorkThreads.emplace_back(&COptimizer::workThread, this, i);
}

void COptimizer::stop ()
{
    unique_lock<mutex> ul(g_MtxLiveReceivers);

    // wait for Companies to finish
    while(m_LiveReceivers > 0)
        allReceived.wait(ul);
    ul.unlock();

    // send suicide signal to all workers

    unique_lock<mutex> ul2(g_MtxLiveWorkers);
    int workers = (int)m_LiveWorkers;
    ul2.unlock();

    for(int i = 0; i < workers; i++){
        g_MtxToSolve.lock();
        m_ToSolve.emplace(nullptr, 0, 0, true);
        toSolveEmpty.notify_all();
        g_MtxToSolve.unlock();
    }

//    unique_lock<mutex> ul3(g_MtxLiveWorkers);
//    while(m_LiveWorkers > 0)
//        workersEmpty.wait(ul3);
//    ul3.unlock();
//
//    int submitters = (int)m_Submitters.size();
//
//    for(int i = 0; i < submitters; i++)
//    {
//        m_Companies[i].m_CompanyMutex.lock();
//        size_t lastId = m_Companies[i].lastId;
//        m_Companies[i].problems.emplace(lastId, AProblemPackWrapper(nullptr, 0, 0, true));
//        m_Companies[i].submitNotReady.notify_one();
//        m_Companies[i].m_CompanyMutex.unlock();
//    }

    for(auto & th : m_Receivers) th.join();
    for(auto & th : m_WorkThreads) th.join();
    for(auto & th : m_Submitters) th.join();
}

void COptimizer::addCompany ( ACompany company )
{
    m_Companies.emplace_back(std::move(company));
}


// TODO: COptimizer implementation goes here
//-------------------------------------------------------------------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__
int main ()
{
  COptimizer optimizer;
  ACompanyTest  company = std::make_shared<CCompanyTest> ();

  optimizer . addCompany ( company );

  optimizer . start ( 30 );
  optimizer . stop  ();
//  if ( ! company -> allProcessed () )
//    throw std::logic_error ( "(some) problems were not correctly processsed" );
  return 0;
}
#endif /* __PROGTEST__ */
