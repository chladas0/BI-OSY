#ifndef __PROGTEST__
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <stdexcept>
using namespace std;

constexpr int                          SECTOR_SIZE                             =             512;
constexpr int                          MAX_RAID_DEVICES                        =              16;
constexpr int                          MAX_DEVICE_SECTORS                      = 1024 * 1024 * 2;
constexpr int                          MIN_DEVICE_SECTORS                      =    1 * 1024 * 2;

constexpr int                          RAID_STOPPED                            = 0;
constexpr int                          RAID_OK                                 = 1;
constexpr int                          RAID_DEGRADED                           = 2;
constexpr int                          RAID_FAILED                             = 3;

struct TBlkDev
{
  int                                  m_Devices;
  int                                  m_Sectors;
  int                               (* m_Read )  ( int, int, void *, int );
  int                               (* m_Write ) ( int, int, const void *, int );
};
#endif /* __PROGTEST__ */


constexpr int                       INIT_TIMESTAMP                          = 1;

class CRaidVolume
{
  public:
    static bool                        create                                  ( const TBlkDev   & dev );
    int                                start                                   ( const TBlkDev   & dev );
    int                                stop                                    ();
    int                                resync                                  ();
    int                                status                                  () const;
    int                                size                                    () const;
    bool                               read                                    ( int               secNr,
                                                                                 void            * data,
                                                                                 int               secCnt );
    bool                               write                                   ( int               secNr,
                                                                                 const void      * data,
                                                                                 int               secCnt );
    void                               init                                    ( const TBlkDev   & dev );

protected:
    int                               m_Status;
    int                               m_Time;
    TBlkDev                           m_Dev;
    int                               m_FailedDev;
};


// Check if all timestamps are the same, except for the failed device
bool allTimeStampsSame(const TBlkDev & dev, const int TimeStamps [], int FailedDev)
{
    int MajorityTimeStamp = -1;

    for(int i = 0; i < dev.m_Devices; i++)
    {
        if(i == FailedDev) continue;

        if(MajorityTimeStamp == -1)
            MajorityTimeStamp = TimeStamps[i];
        else if(MajorityTimeStamp != TimeStamps[i])
            return false;
    }

    return true;
}

void CRaidVolume::init ( const TBlkDev & dev )
{
    m_Dev = dev;
    m_Status = RAID_OK;
    m_Time = INIT_TIMESTAMP;
    m_FailedDev = -1;
}

bool CRaidVolume::create ( const TBlkDev & dev )
{
    unsigned char buffer[SECTOR_SIZE] = {0};
    memcpy(buffer, &INIT_TIMESTAMP, sizeof(INIT_TIMESTAMP));

    bool diskFailed = false;

    for(int i = 0; i < dev.m_Devices; i++)
        if(dev.m_Write(i, dev.m_Sectors - 1, buffer, 1) != 1)
        {
            if(diskFailed)
                return false;
            else
                diskFailed = true;
        }

    return true;
}

int CRaidVolume::start ( const TBlkDev & dev )
{
    init(dev);

    int TimeStamps[MAX_RAID_DEVICES];
    int buffer[SECTOR_SIZE] = {0};

    bool diskFailed = false;

    // read the timestamps from all disks
    for(int i = 0; i < dev.m_Devices; i++)
        if(dev.m_Read(i, dev.m_Sectors - 1, buffer, 1) != 1)
        {
            if(diskFailed)
                return m_Status = RAID_FAILED;
            else
            {
                diskFailed = true;
                m_FailedDev = i;
            }
        }
        else
            memcpy(&TimeStamps[i], buffer, sizeof(int));

    // Reading failed for one disk, if all other disks have the same timestamp, the raid is just degraded
    // otherwise it is failed

    if(diskFailed)
    {
        if(allTimeStampsSame(dev, TimeStamps, m_FailedDev))
            return m_Status = RAID_DEGRADED;
        else
            return m_Status = RAID_FAILED;
    }

    // If reading was successful for all disks, check if all timestamps are the same
    if(allTimeStampsSame(dev, TimeStamps, -1))
        return m_Status = RAID_OK;

    // If not all timestamps are the same, the raid is either degraded or failed
    // we need to check how many timestamps are different from the majority timestamp
    // if it is more than one the raid is failed, otherwise it is degraded

    int FirstDifferent = TimeStamps[0], SecondDifferent = -1;
    int countFirst = 0;

    for(int i = 0; i < dev.m_Devices; i++)
    {
        if(TimeStamps[i] == FirstDifferent)
            countFirst++;

        else if(SecondDifferent == -1)
            SecondDifferent = TimeStamps[i];

        else if(TimeStamps[i] != SecondDifferent)
            return m_Status = RAID_FAILED;
    }

    int Majority = countFirst == 1 ? SecondDifferent : FirstDifferent;

    for(int i = 0; i < dev.m_Devices; i++)
        if(TimeStamps[i] != Majority)
            m_FailedDev = i;

    return m_Status = RAID_DEGRADED;
}

int CRaidVolume::stop ()
{
    // todo
}

int CRaidVolume::resync ()
{
    // todo
}

int CRaidVolume::status () const
{
    return m_Status;
}

int CRaidVolume::size () const
{
    // the last sector on each disk is reserved for timestamp data
    return m_Dev.m_Devices * (m_Dev.m_Sectors - 1);
}

bool CRaidVolume::read ( int secNr, void * data, int secCnt )
{
    // todo implement raid write, find the disk, sector and place for parity
}

bool CRaidVolume::write ( int secNr, const void * data, int secCnt )
{
    // todo

}


#ifndef __PROGTEST__
#include "tests.cpp"

#endif /* __PROGTEST__ */
