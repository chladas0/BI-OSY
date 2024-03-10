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


struct CProblemPackWrapper
{
    shared_ptr<CProblemPack> m_Pack;
    int m_ProblemId = -1;
    int m_CompanyId = -1;
    bool m_Suicide = false;
public:
    CProblemPackWrapper() = default;
    CProblemPackWrapper(shared_ptr<CProblemPack> pack, int problemId, int companyId, bool suicide = false)
        : m_Pack(std::move(pack)), m_ProblemId(problemId), m_CompanyId(companyId), m_Suicide(suicide)
    {}
};


//-------------------------------------------------------------------------------------------------------------------------------------------------------------
class COptimizer
{
  public:
    static bool usingProgtestSolver (){
      return true;
    }

    static void checkAlgorithmMin (APolygon p){
      // dummy implementation if usingProgtestSolver() returns true
    }

    static void checkAlgorithmCnt (APolygon p){
      // dummy implementation if usingProgtestSolver() returns true
    }

    void start (int threadCount);
    void stop ();
    void addCompany (ACompany company);

    static void workThread (int threadId, COptimizer * optimizer);
    static void problemReceiver (int companyId, COptimizer * optimizer);
    static void problemSubmitter (int companyId, COptimizer * optimizer);

    class CProgtestSolverWrapper
    {
    public:
        CProgtestSolverWrapper();
        void addToSolve(CProblemPackWrapper & pack);
        void solveOneMin(APolygon & polygon);
        void solveOneCnt(APolygon & polygon);
    private:

        vector<AProgtestSolver> m_Min;
        vector<AProgtestSolver> m_Cnt;

        queue<CProblemPackWrapper> toReview;

        int idx_Min = 0;
        int idx_Cnt = 0;

        mutex m_MtxMinSolvers;
        mutex m_MtxCntSolvers;
    };

private:
    CProgtestSolverWrapper solver;

    size_t m_LiveWorkers;
    size_t m_LiveReceivers;

    vector<ACompany> m_Companies;

    vector<thread>  m_WorkThreads;
    vector<thread>  m_Receivers;
    vector<thread>  m_Submitters;

    queue<CProblemPackWrapper> m_ToSolve;
    vector<CProblemPackWrapper> m_ToSubmit;

    mutex g_MtxToSolve;
    mutex g_MtxToSubmit;
    mutex g_MtxLiveReceivers;


    condition_variable allReceived;
    condition_variable toSolveEmpty;
    condition_variable toSubmitEmpty;
};
// ---------------------------------------------------------------------------------------------------------------------
COptimizer::CProgtestSolverWrapper::CProgtestSolverWrapper()
{
    m_Min.push_back(createProgtestMinSolver());
    m_Cnt.push_back(createProgtestCntSolver());
}

void COptimizer::CProgtestSolverWrapper::addToSolve (CProblemPackWrapper & pack)
{
    for(auto & i : pack.m_Pack->m_ProblemsCnt)
        solveOneMin(i);

    for(auto & i : pack.m_Pack->m_ProblemsMin)
        solveOneCnt(i);
}

void COptimizer::CProgtestSolverWrapper::solveOneMin (APolygon & polygon)
{
    unique_lock<mutex> ul(m_MtxMinSolvers);
    if(!m_Min[idx_Min]->addPolygon(polygon)){
        m_Min.push_back(createProgtestMinSolver());
        idx_Min++;
        m_Min[idx_Min]->addPolygon(polygon);
        ul.unlock();

        m_Min[idx_Min-1]->solve();
    }
}

void COptimizer::CProgtestSolverWrapper::solveOneCnt (APolygon & polygon)
{
    unique_lock<mutex> ul(m_MtxCntSolvers);
    if(!m_Cnt[idx_Cnt]->addPolygon(polygon))
    {
        // Create new solver for other threads
        m_Cnt.push_back(createProgtestCntSolver());
        idx_Cnt++;
        m_Cnt[idx_Cnt]->addPolygon(polygon);
        ul.unlock();

        // this thread will be solving
        size_t solved = m_Cnt[idx_Cnt-1]->solve();

        while(!toReview.empty())
        {
            auto cur = toReview.front();
        }

    }
}


void COptimizer::workThread (int threadId, COptimizer * opt)
{
    printf("Work thread %d started \n", threadId);

    while(true)
    {
        unique_lock<mutex> toSolveLock(opt->g_MtxToSolve);

        while (opt->m_ToSolve.empty())
            opt->toSolveEmpty.wait(toSolveLock);

        auto pack = opt->m_ToSolve.front();
        opt->m_ToSolve.pop();


        if(pack.m_Suicide){
            printf("SUICIDE SINGNAL \n");
            break;
        }

        opt->solver.addToSolve(pack);

        // needs to have the 2 flags
        opt->g_MtxToSubmit.lock();
        opt->m_ToSubmit.push_back(pack);
        opt->g_MtxToSubmit.unlock();

        printf("Processed\n");
    }

    printf("Workthread %d ended\n", threadId);
}

void COptimizer::problemReceiver (int companyId, COptimizer * opt)
{
  size_t id = 0;

  while(true)
  {
    auto pack = opt->m_Companies[companyId]->waitForPack();

    if(pack == nullptr) break;

    // Emplace the pack to be solved
    opt->g_MtxToSolve.lock();

    opt->m_ToSolve.emplace(pack, id, companyId);
    opt->toSolveEmpty.notify_all();

    opt->g_MtxToSolve.unlock();
    id++;
  }

  // update the live receivers

  opt->g_MtxLiveReceivers.lock();

  opt->m_LiveReceivers--;
  if(opt->m_LiveReceivers == 0)
        opt->allReceived.notify_all();

  opt->g_MtxLiveReceivers.unlock();

  printf("All problems received\n");
}

void COptimizer::problemSubmitter (int companyId, COptimizer * opt)
{
    // TODO
}


void COptimizer::start ( int threadCount )
{
    m_LiveWorkers = threadCount;
    m_LiveReceivers = m_Companies.size();

    for(int i = 0; i < (int)m_Companies.size(); i++){
        m_Receivers.emplace_back(problemReceiver, i, this);
        m_Submitters.emplace_back(problemSubmitter, i, this);
    }

    for (int i = 0; i < threadCount; i++)
        m_WorkThreads.emplace_back(workThread, i, this);
}

void COptimizer::stop ()
{
    unique_lock<mutex> ul(g_MtxLiveReceivers);

    // wait for Companies to finish
    while(m_LiveReceivers > 0)
        allReceived.wait(ul);
    ul.unlock();

    // send suicide signal to all workers
    for(int i = 0; i < (int)m_LiveWorkers; i++){
        g_MtxToSolve.lock();
        m_ToSolve.emplace(nullptr, 0, 0, true);
        toSolveEmpty.notify_all();
        g_MtxToSolve.unlock();
    }

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
  optimizer . start ( 100 );
  optimizer . stop  ();
//  if ( ! company -> allProcessed () )
//    throw std::logic_error ( "(some) problems were not correctly processsed" );
  return 0;
}
#endif /* __PROGTEST__ */
