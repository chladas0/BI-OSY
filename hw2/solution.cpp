#ifndef __PROGTEST__
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <stdexcept>
using namespace std;

constexpr int                          SECTOR_SIZE                             =             5;
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
    CRaidVolume() : m_Status(RAID_OK), m_Time(INIT_TIMESTAMP), m_FailedDev(-1) {}

    static bool                        create             ( const TBlkDev & dev );
    int                                start              ( const TBlkDev & dev );
    int                                stop               ();
    int                                resync             ();
    bool                               read               ( int secNr, void * data, int secCnt );
    bool                               write              ( int secNr, const void * data, int secCnt );
    int                                status             () const;
    int                                size               () const;

    void                               setDegradedTesting (int failedDisk );
    void                               findDiskAndStripe  (int secNr, int & diskForData, int & diskForParity,
                                                           int & stripe) const;


private:
    bool                               readAndUpdate      (int disk, int secNr, void * data, int secCnt);
    bool                               writeAndUpdate     (int disk, int secNr, const void * data, int secCnt);

    bool                               readDataOK         (int & sector, char * & data, int secEnd);
    bool                               readDataDegraded   (int sector, char * data, int secEnd);

    bool                               writeDataOK        (int & sector, const char * & data, int secEnd);
    bool                               writeDataDegraded  (int sector, const char * data, int secEnd);

    void                               buffersClear       ();

    static int                         writeTimeStamp     (const TBlkDev & dev, int m_Time, int failedDevice = -1);

private:

    int                               m_Status;
    int                               m_Time;
    TBlkDev                           m_Dev;
    int                               m_FailedDev;
    unsigned char                     m_Buffer[SECTOR_SIZE];
    unsigned char                     m_OldData[SECTOR_SIZE];
};




bool CRaidVolume::read ( int secNr, void * dataVoid, int secCnt )
{
    char * data = (char*)dataVoid;
    int endSec = min(size(), secNr + secCnt);

    switch (m_Status)
    {
        case RAID_OK:
            if(readDataOK(secNr, data, endSec))
                return true;

        case RAID_DEGRADED:
            if(readDataDegraded(secNr, data, endSec))
                return true;

        default:
            return false;
    }
}


bool CRaidVolume::write ( int secNr, const void * dataVoid, int secCnt )
{
    const char * data = (char*)dataVoid;
    int endSec = min(size(), secNr + secCnt);

    switch (m_Status)
    {
        case RAID_OK:
            if (writeDataOK(secNr, data, endSec))
                return true;

        case RAID_DEGRADED:
            if(writeDataDegraded(secNr, data, endSec))
                return true;

        default:
            return false;
    }
}


bool CRaidVolume::readDataOK(int & sector, char * & data, int secEnd)
{
    int diskForData, diskForParity, stripe;

    for(; sector < secEnd; sector++)
    {
        findDiskAndStripe(sector, diskForData, diskForParity, stripe);

        if(!readAndUpdate(diskForData, stripe, data, 1))
            return false;

        data += SECTOR_SIZE;
    }

    return true;
}


bool CRaidVolume::readDataDegraded(int sector, char * data, int secEnd)
{
    int diskForData, diskForParity, stripe;

    for(; sector < secEnd; sector++)
    {
        findDiskAndStripe(sector, diskForData, diskForParity, stripe);
        buffersClear();

        // Reading from the failed device, using XORed data from other devices
        if(diskForData == m_FailedDev)
        {
            unsigned char xorBuffer[SECTOR_SIZE] = {0};

            for(int i = 0; i < m_Dev.m_Devices; i++)
            {
                if(i == m_FailedDev) continue;

                if(!readAndUpdate(i, stripe, m_Buffer, 1))
                    return false;

                for(int j = 0; j < SECTOR_SIZE; j++)
                    xorBuffer[j] ^= m_Buffer[j];
            }

            memcpy(data, xorBuffer, SECTOR_SIZE);
        }
        else
            if(!readAndUpdate(diskForData, stripe, data, 1))
                return false;

        data += SECTOR_SIZE;
    }
    return true;
}


bool CRaidVolume::writeDataDegraded(int sector, const char * data, int secEnd)
{
    int diskForData, diskForParity, stripe;

    for(; sector < secEnd; sector++)
    {
        findDiskAndStripe(sector, diskForData, diskForParity, stripe);
        buffersClear();

        // Read the data from not failed disk
        if(diskForData != m_FailedDev)
        {
            // Reads the old data, writes the new
            if(!readAndUpdate(diskForData, stripe, m_OldData, 1) || !writeAndUpdate(diskForData, stripe, data, 1))
                return false;

            // If the disk for parity is failed we won't write it, it can be synced from the other disks
            if(m_FailedDev == diskForParity)
            {
                data += SECTOR_SIZE;
                continue;
            }

            // Read the parity, un xor the old data, add the new
            if(!readAndUpdate(diskForParity, stripe, m_Buffer, 1))
                return false;

            for(int j = 0; j < SECTOR_SIZE; j++)
                m_Buffer[j] = m_Buffer[j] ^ m_OldData[j] ^ data[j];

            if(!writeAndUpdate(diskForParity, stripe, m_Buffer, 1))
                return false;
        }
        else
        {
            // Retrieve the data using other disks
            for(int i = 0; i < m_Dev.m_Devices; i++)
            {
                if(i == m_FailedDev) continue;

                if(!readAndUpdate(i, stripe, m_Buffer, 1))
                    return false;

                for(int j = 0; j < SECTOR_SIZE; j++)
                    m_OldData[j] ^= m_Buffer[j];
            }

            // Read the old parity
            if(!readAndUpdate(diskForParity, stripe, m_Buffer, 1))
                return false;

            // Update parity with new Data
            for(int j = 0; j < SECTOR_SIZE; j++)
                m_Buffer[j] = m_Buffer[j] ^ m_OldData[j] ^ data[j];

            // Write the parity
            if(!writeAndUpdate(diskForParity, stripe, m_Buffer, 1))
                return false;
        }
        data += SECTOR_SIZE;
    }
    return true;
}


bool CRaidVolume::writeDataOK(int & sector, const char * & data, int secEnd)
{
    int diskForData, diskForParity, stripe;

    for(; sector < secEnd; sector++)
    {
        findDiskAndStripe(sector, diskForData, diskForParity, stripe);
        buffersClear();

        // Read the old data, overwrite with new data
        if(!readAndUpdate(diskForData, stripe, m_OldData, 1) || !writeAndUpdate(diskForData, stripe, data, 1) ||
           !readAndUpdate(diskForParity, stripe, m_Buffer, 1))
            return false;

        // Update the parity
        for(int j = 0; j < SECTOR_SIZE; j++)
            m_Buffer[j] = m_Buffer[j] ^ m_OldData[j] ^ data[j];

        // Write the parity
        if(!writeAndUpdate(diskForParity, stripe, m_Buffer, 1))
            return false;

        data += SECTOR_SIZE;
    }
    return true;
}


void CRaidVolume::findDiskAndStripe(int secNr, int & diskForData, int & diskForParity, int & stripe) const
{
    stripe = secNr / (m_Dev.m_Devices - 1);
    diskForParity = 3 - (stripe % 4);
    diskForData = secNr % (m_Dev.m_Devices - 1);
    if(diskForData >= diskForParity) diskForData++;
}


// Check if all timestamps are the same, except for the failed device
bool allTimeStampsSame(const TBlkDev & dev, const int TimeStamps [], int FailedDev)
{
    int MajorityTimeStamp = -1;

    for(int i = 0; i < dev.m_Devices; i++){
        if(i == FailedDev) continue;

        if(MajorityTimeStamp == -1)
            MajorityTimeStamp = TimeStamps[i];
        else if(MajorityTimeStamp != TimeStamps[i])
            return false;
    }

    return true;
}


int CRaidVolume::writeTimeStamp(const TBlkDev & dev, int time, int failedDevice)
{
    unsigned char buffer[SECTOR_SIZE] = {0};
    memcpy(buffer, &time, sizeof(time));

    int failedWrites = 0;

    for(int i = 0; i < dev.m_Devices; i++)
    {
        if(i == failedDevice) continue;

        if(dev.m_Write(i, dev.m_Sectors - 1, buffer, 1) != 1)
            failedWrites++;
    }

    return failedWrites;    
}


bool CRaidVolume::create ( const TBlkDev & dev )
{
    return writeTimeStamp(dev, INIT_TIMESTAMP) < 1;
}


int CRaidVolume::start ( const TBlkDev & dev )
{
    m_Dev = dev; m_FailedDev = -1; m_Status = RAID_OK;

    int TimeStamps[MAX_RAID_DEVICES] = {0};
    buffersClear();

    // read the timestamps from all disks
    for(int i = 0; i < dev.m_Devices; i++)
        if(readAndUpdate(i, dev.m_Sectors - 1, m_Buffer, 1))
            memcpy(&TimeStamps[i], m_Buffer, sizeof(int));

    if(m_Status == RAID_FAILED)
        return m_Status;


    m_Time = TimeStamps[0];

    // If we are in degraded state, then if all are the same we are just degraded, otherwise failed
    if(m_Status == RAID_DEGRADED)
    {
        if(allTimeStampsSame(dev, TimeStamps, m_FailedDev))
            return m_Status = RAID_DEGRADED;
        else
            return m_Status = RAID_FAILED;
    }


    // We are in OK state
    int FirstDifferent = m_Time, SecondDifferent = -1;
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

    m_Time = Majority;
    return m_FailedDev == - 1 ? m_Status = RAID_OK : m_Status = RAID_DEGRADED;
}


int CRaidVolume::stop ()
{
    m_Time += 1;
    writeTimeStamp(m_Dev, m_Time, m_FailedDev);

    return m_Status = RAID_STOPPED;
}


int CRaidVolume::resync ()
{
    if(m_Status != RAID_DEGRADED)
        return m_Status;

    for(int i = 0; i < m_Dev.m_Sectors - 1; i++)
    {
        unsigned char xorBuffer[SECTOR_SIZE] = {0};

        for(int j = 0; j < m_Dev.m_Devices; j++)
        {
            if(j == m_FailedDev) continue;

            if(m_Dev.m_Read(j, i, m_Buffer, 1) != 1)
                return m_Status = RAID_FAILED;

            for(int k = 0; k < SECTOR_SIZE; k++)
                xorBuffer[k] ^= m_Buffer[k];
        }

        if (m_Dev.m_Write(m_FailedDev, i, xorBuffer, 1) != 1)
            return m_Status = RAID_DEGRADED;
    }

    // write the timestamp to the last sector
    buffersClear();
    memcpy(m_Buffer, &m_Time, sizeof(m_Time));

    if (m_Dev.m_Write(m_FailedDev, m_Dev.m_Sectors - 1, m_Buffer, 1) != 1)
        return m_Status = RAID_DEGRADED;

    m_FailedDev = -1;
    return m_Status = RAID_OK;
}


void CRaidVolume::buffersClear()
{
    memset(m_Buffer, 0, SECTOR_SIZE);
    memset(m_OldData, 0, SECTOR_SIZE);
}


bool CRaidVolume::readAndUpdate(int disk, int secNr, void * data, int secCnt)
{
    if(m_Dev.m_Read(disk, secNr, data, secCnt) == secCnt)
        return true;

    if(m_FailedDev == -1)
    {
        m_FailedDev = disk;
        m_Status = RAID_DEGRADED;
    }
    else
        m_Status = RAID_FAILED;
    return false;
}


bool CRaidVolume::writeAndUpdate(int disk, int secNr, const void *data, int secCnt)
{
    if(m_Dev.m_Write(disk, secNr, data, secCnt) == secCnt)
        return true;

    if(m_FailedDev == -1)
    {
        m_FailedDev = disk;
        m_Status = RAID_DEGRADED;
    }
    else
        m_Status = RAID_FAILED;
    return false;
}


int CRaidVolume::status () const
{
    return m_Status;
}


void CRaidVolume::setDegradedTesting(int failedDisk)
{
    m_FailedDev = failedDisk;
    m_Status = RAID_DEGRADED;
}


// the last sector on each disk is reserved for timestamp data, and space of one disk is reserved for parity
int CRaidVolume::size () const
{
    return (m_Dev.m_Devices - 1) * (m_Dev.m_Sectors - 1);
}


#ifndef __PROGTEST__
#include "tests3.cpp"
#endif /* __PROGTEST__ */
