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
    size_t m_ProblemId, m_CompanyId;
    size_t m_MinSize, m_CntSize;
    bool m_Suicide = false;



    CProblemPackWrapper(shared_ptr<CProblemPack> pack, size_t problemId, size_t companyId, bool suicide = false)
        : m_Pack(std::move(pack)), m_ProblemId(problemId), m_CompanyId(companyId), m_Suicide(suicide)
    {
        if(pack){
            m_MinSize = m_Pack->m_ProblemsMin.size(); m_CntSize = m_Pack->m_ProblemsCnt.size();
        }
        else
        {
            m_MinSize = 0;
            m_CntSize = 0;
        }
    }
    CProblemPackWrapper() = default;

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
    unordered_map<int, CProblemPackWrapper> problems;
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

    void addToSolve(const CProblemPackWrapper& pack);
    void solveOneCnt(const APolygon& polygon);
    void solveOneMin(const APolygon& polygon);
    void initSolver(){m_MinSolvers.emplace_back(createProgtestMinSolver()),
                      m_CntSolvers.emplace_back(createProgtestCntSolver());}

private:
    vector<thread>  m_WorkThreads;
    vector<thread>  m_Receivers;
    vector<thread>  m_Submitters;

    vector<ACompanyWrapper> m_Companies;
    vector<CProblemPackWrapper> m_toReview;

    size_t m_LiveWorkers;
    size_t m_LiveReceivers;

    queue<CProblemPackWrapper> m_ToSolve;

    mutex g_MtxToSolve;
    mutex g_MtxLiveReceivers;
    mutex g_Solvers;
    mutex g_ToReview;

    condition_variable allReceived;
    condition_variable toSolveEmpty;
    condition_variable toSubmitEmpty;

    // solver stuff
    size_t cntIdx = 0;
    size_t minIdx = 0;
    vector<AProgtestSolver> m_MinSolvers;
    vector<AProgtestSolver> m_CntSolvers;
};


// Solving -------------------------------------------------------------------------------------------------------------
void COptimizer::addToSolve(const CProblemPackWrapper& pack)
{
    g_ToReview.lock();
    m_toReview.emplace_back(pack);
    g_ToReview.unlock();

    for(auto & i : pack.m_Pack->m_ProblemsMin) solveOneMin(i);
    for(auto & i : pack.m_Pack->m_ProblemsCnt) solveOneCnt(i);
}

void COptimizer::solveOneCnt(const APolygon& polygon)
{
    unique_lock<mutex> ul(g_Solvers);
    m_CntSolvers[cntIdx]->addPolygon(polygon);
}

void COptimizer::solveOneMin(const APolygon& polygon)
{
    unique_lock<mutex> ul(g_Solvers);
    m_MinSolvers[minIdx]->addPolygon(polygon);
}



// ---------------------------------------------------------------------------------------------------------------------
void COptimizer::workThread (int threadId)
{
    printf("Work thread %d started \n", threadId);

    while(true)
    {
        unique_lock<mutex> toSolveLock(g_MtxToSolve);

        while (m_ToSolve.empty())
            toSolveEmpty.wait(toSolveLock);

        auto pack = m_ToSolve.front();
        m_ToSolve.pop();


        if(pack.m_Suicide){
            printf("SUICIDE SINGNAL \n");
            break;
        }

        addToSolve(pack);
        printf("Processed\n");
    }

    printf("Workthread %d ended\n", threadId);
}

void COptimizer::problemReceiver (int companyId)
{
  size_t id = 0;

  while(true)
  {
    auto pack = m_Companies[companyId].company->waitForPack();

    if(pack == nullptr) break;

    CProblemPackWrapper x(pack, id, companyId);

    // Emplace the pack to be solved
    g_MtxToSolve.lock();
    m_ToSolve.emplace(x);
    toSolveEmpty.notify_all();
    g_MtxToSolve.unlock();

    // Add it to the solved problems
    m_Companies[companyId].m_CompanyMutex.lock();
    m_Companies[companyId].problems.emplace(id, x);
    m_Companies[companyId].m_CompanyMutex.unlock();

    id++;
  }

  // update the live receivers
  g_MtxLiveReceivers.lock();

  m_LiveReceivers--;
  if(m_LiveReceivers == 0)
        allReceived.notify_all();

  g_MtxLiveReceivers.unlock();

  printf("All problems received\n");
}

void COptimizer::problemSubmitter (int companyId)
{

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
  optimizer . start ( 5 );
  optimizer . stop  ();
//  if ( ! company -> allProcessed () )
//    throw std::logic_error ( "(some) problems were not correctly processsed" );
  return 0;
}
#endif /* __PROGTEST__ */
