/* SW RAID5 - basic test
 *
 * The testing of the RAID driver requires a backend (simulating the underlying disks).
 * Next, the tests of your RAID implemetnation are needed. To help you with the implementation,
 * a sample backend is implemented in this file. It provides a quick-and-dirty
 * implementation of the underlying disks (simulated in files) and a few Raid... function calls.
 *
 * The implementation in the real testing environment is different. The sample below is a
 * minimalistic disk backend which matches the required interface. The backend, for instance,
 * cannot simulate a crashed disk. To test your Raid implementation, you will have to modify
 * or extend the backend.
 *
 * Next, you will have to add some raid testing. There is a few Raid... functions called from within
 * main(), however, the tests are incomplete. For instance, RaidResync () is not tested here. Once
 * again, this is only a starting point.
 */

#include <iostream>
using namespace std;

constexpr int                          RAID_DEVICES = 4;
constexpr int                          DISK_SECTORS = 8192;
static FILE                          * g_Fp[RAID_DEVICES];

//-------------------------------------------------------------------------------------------------
/** Sample sector reading function. The function will be called by your Raid driver implementation.
 * Notice, the function is not called directly. Instead, the function will be invoked indirectly
 * through function pointer in the TBlkDev structure.
 */
int                                    diskRead                                ( int                                   device,
                                                                                 int                                   sectorNr,
                                                                                 void                                * data,
                                                                                 int                                   sectorCnt )
{
    if ( device < 0 || device >= RAID_DEVICES )
        return 0;
    if ( g_Fp[device] == nullptr )
        return 0;
    if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS )
        return 0;
    fseek ( g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET );
    return fread ( data, SECTOR_SIZE, sectorCnt, g_Fp[device] );
}
//-------------------------------------------------------------------------------------------------
/** Sample sector writing function. Similar to diskRead
 */
int                                    diskWrite                               ( int                                   device,
                                                                                 int                                   sectorNr,
                                                                                 const void                          * data,
                                                                                 int                                   sectorCnt )
{
    if ( device < 0 || device >= RAID_DEVICES )
        return 0;
    if ( g_Fp[device] == NULL )
        return 0;
    if ( sectorCnt <= 0 || sectorNr + sectorCnt > DISK_SECTORS )
        return 0;
    fseek ( g_Fp[device], sectorNr * SECTOR_SIZE, SEEK_SET );
    return fwrite ( data, SECTOR_SIZE, sectorCnt, g_Fp[device] );
}
//-------------------------------------------------------------------------------------------------
/** A function which releases resources allocated by openDisks/createDisks
 */
void                                   doneDisks                               ()
{
    for ( int i = 0; i < RAID_DEVICES; i ++ )
        if ( g_Fp[i] )
        {
            fclose ( g_Fp[i] );
            g_Fp[i]  = nullptr;
        }
}
//-------------------------------------------------------------------------------------------------
/** A function which creates the files needed for the sector reading/writing functions above.
 * This function is only needed for the particular implementation above.
 */
TBlkDev                                createDisks                             ()
{
    char       buffer[SECTOR_SIZE];
    TBlkDev    res;
    char       fn[100];

    memset    ( buffer, 0, sizeof ( buffer ) );
    for ( int i = 0; i < RAID_DEVICES; i ++ )
    {
        snprintf ( fn, sizeof ( fn ), "/tmp/%04d", i );
        g_Fp[i] = fopen ( fn, "w+b" );
        if ( ! g_Fp[i] )
        {
            doneDisks ();
            throw std::runtime_error ( "Raw storage create error" );
        }

        for ( int j = 0; j < DISK_SECTORS; j ++ )
            if ( fwrite ( buffer, sizeof ( buffer ), 1, g_Fp[i] ) != 1 )
            {
                doneDisks ();
                throw std::runtime_error ( "Raw storage create error" );
            }
    }

    res . m_Devices = RAID_DEVICES;
    res . m_Sectors = DISK_SECTORS;
    res . m_Read    = diskRead;
    res . m_Write   = diskWrite;
    return res;
}
//-------------------------------------------------------------------------------------------------
/** A function which opens the files needed for the sector reading/writing functions above.
 * This function is only needed for the particular implementation above.
 */
TBlkDev                                openDisks                               ()
{
    TBlkDev    res;
    char       fn[100];

    for ( int i = 0; i < RAID_DEVICES; i ++ )
    {
        snprintf ( fn, sizeof ( fn ), "/tmp/%04d", i );
        g_Fp[i] = fopen ( fn, "r+b" );
        if ( ! g_Fp[i] )
        {
            doneDisks ();
            throw std::runtime_error ( "Raw storage access error" );
        }
        fseek ( g_Fp[i], 0, SEEK_END );
        if ( ftell ( g_Fp[i] ) != DISK_SECTORS * SECTOR_SIZE )
        {
            doneDisks ();
            throw std::runtime_error ( "Raw storage read error" );
        }
    }
    res . m_Devices = RAID_DEVICES;
    res . m_Sectors = DISK_SECTORS;
    res . m_Read    = diskRead;
    res . m_Write   = diskWrite;
    return res;
}
//-------------------------------------------------------------------------------------------------
void                                   test1                                   ()
{
    /* create the disks before we use them
     */
    TBlkDev  dev = createDisks ();
    /* The disks are ready at this moment. Your RAID-related functions may be executed,
     * the disk backend is ready.
     *
     * First, try to create the RAID:
     */

    assert ( CRaidVolume::create ( dev ) );

    /* start RAID volume */

    CRaidVolume vol;

    // Adhoc testing

    cout << vol.start(dev) << endl;

    assert ( vol . start ( dev ) == RAID_OK );
    assert ( vol . status () == RAID_OK );

    int i, retCode;

    for ( i = 0; i < vol.size () -1; i ++ )
    {
        char buffer [SECTOR_SIZE];
        memset(buffer,0,sizeof(buffer));
        sprintf(buffer, "%d", i);

        if(i == 24571)
        {
            for(int a = 0; a < SECTOR_SIZE; a++)
                cout << buffer[a] << " ";
            cout << endl;
        }

        retCode = vol.write ( i, buffer, 1 );
    }

    if(vol.status() != RAID_OK) printf("Error, write");
    char buf[SECTOR_SIZE];

    retCode = vol.read(24571, buf, 1);

    if(retCode > 0)
    {
        int a = atoi(buf);
        if(a == 24571) printf("YES\n");
        else
            printf("ERROR write or read, a is : %d:%s", a,buf);
    }
    else printf("Error Read\n");

    //testing write and read
    for ( i = 0; i < vol.size(); i ++ )
    {
        char buffer [SECTOR_SIZE];
        retCode = vol.read( i, buffer, 1 );
        retCode = vol.write( i, buffer, 1 );
    }
    if(vol.status() != RAID_OK) printf("Error, write");
    memset(buf, 0, sizeof(buf));

    retCode = vol.read(24571, buf, 1);

    if(retCode > 0)
    {
        int a = atoi(buf);
        if(a == 24571) printf("YES\n");
        else
            printf("ERROR write or read, a is : %d:%s", a,buf);
    }
    else printf("Error Read\n");

    diskWrite(2, 0, "Killed", 1);
    int status = vol.status();
    retCode = RaidRead(24571, buf, 1);
    if(retCode > 0)
    {
        int a = atoi(buf);
        if(a == 24571) printf("YES\n");
        else
            printf("ERROR write or read, a is : %d:%s", a,buf);
    }
    else printf("Error Read\n");
    retCode = RaidResync();

    // Adhoc testing
    /*
    char buffer [SECTOR_SIZE];
    memset ( buffer, 0, sizeof ( buffer ) );

    int XOR = 0;
    char Data [] = {8, 16 , 32};

    for ( int i = 0; i < dev . m_Devices; i ++ )
    {
        if(i != dev.m_Devices - 1){
            buffer[0] = Data[i];
            XOR ^= buffer[0];
        }
        else
            buffer[0] = XOR;

        assert ( dev . m_Write ( i, 0, buffer, 1 ) == 1 );
    }

    // show the data writen
    for ( int i = 0; i < dev . m_Devices; i ++ )
    {
        assert ( dev . m_Read ( i, 0, buffer, 1 ) == 1 );
        for ( int j = 0; j < 10; j ++ )
            printf("%d ", buffer[j]);
        printf("\n");
    }

    printf("---------------------------\n");

    // damage the data on disk 1
    buffer[0] = 42;
    assert ( dev . m_Write ( 1, 0, buffer, 1 ) == 1 );
    vol.setDegradedTesting(1);

    // show the data writen
    for ( int i = 0; i < dev . m_Devices; i ++ )
    {
        assert ( dev . m_Read ( i, 0, buffer, 1 ) == 1 );
        for ( int j = 0; j < 10; j ++ )
            printf("%d ", buffer[j]);
        printf("\n");
    }

    auto state = vol.resync();

    printf("---------------------------\n");

    printf("State after resync: ");

    switch (state)
    {
        case RAID_OK:
            printf("RAID_OK\n");
            break;
        case RAID_DEGRADED:
            printf("RAID_DEGRADED\n");
            break;
        case RAID_FAILED:
            printf("RAID_FAILED\n");
            break;
        case RAID_STOPPED:
            printf("RAID_STOPPED\n");
            break;
    }

    // show the data writen
    for ( int i = 0; i < dev . m_Devices; i ++ )
    {
        assert ( dev . m_Read ( i, 0, buffer, 1 ) == 1 );
        for ( int j = 0; j < 10; j ++ )
            printf("%d ", buffer[j]);
        printf("\n");
    }

    printf("---------------------------\n");

    for(int i = 0; i < 13; i++)
    {
        int secNr = i;
        int dataDisk, parityDisk, sector;

        vol.findDiskAndStripe(secNr, dataDisk, parityDisk, sector);

        printf("Searching for Sector: %d\n", secNr);
        printf("Data disk: %d\n", dataDisk);
        printf("Parity disk: %d\n", parityDisk);
        printf("Index on disk: %d\n", sector);
        printf("---------------------------\n");
    }

    // Testing readDataOK

    for(int i = 0; i < 3; i++){
        vol.read(i, buffer, 1);
        printf("Data at sector %d: %d\n", i, buffer[0]);
    }

    memset(buffer, 0, sizeof(buffer));
    vol.setDegradedTesting(2);

    printf("Testing normal RAID\n");
    for(int i = 0; i < 4; i++)
    {
        memset(buffer, i, sizeof(buffer));
        vol.write(i, buffer, 1);
    }

    for(int i = 0; i < 4; i++)
    {
        vol.read(i, buffer, 1);

        for(int j = 0; j < 5; j++)
            printf("%d ", buffer[j]);

        printf("\n");
    }

    // damage the data on disk 2
    printf("Damaging disk 2\n");
    for(int i = 0; i < SECTOR_SIZE; i++)
        buffer[i] = i;

    assert ( dev . m_Write ( 2, 0, buffer, 1 ) == 1 );

    printf("---------------------------\n");
    printf("Testing degraded RAID\n");

    for(int i = 0; i < 4; i++)
    {
        vol.read(i, buffer, 1);

        for(int j = 0; j < 5; j++)
            printf("%d ", buffer[j]);

        printf("\n");
    }

    printf("---------------------------\n");
    printf("RAW data stored on first 4 sectors \n");
    for(int i = 0; i < 4; i++)
    {
        dev.m_Read(i, 0, buffer, 1);

        for(int j = 0; j < 5; j++)
            printf("%d ", buffer[j]);

        printf("\n");
    }

    printf("---------------------------\n");
    printf("Resyncing RAID\n");
    vol.resync();


    printf("RAW data stored on first 4 sectors after Resync\n");
    for(int i = 0; i < 4; i++)
    {
        dev.m_Read(i, 0, buffer, 1);

        for(int j = 0; j < 5; j++)
            printf("%d ", buffer[j]);

        printf("\n");
    }

     Lada testing :D
    for ( int i = 0; i < vol . size (); i ++ )
    {
        char buffer [SECTOR_SIZE];

        assert ( vol . read ( i, buffer, 1 ) );
        assert ( vol . write ( i, buffer, 1 ) );
    }
    */


    // Test writing OK




    /* Stop the raid device ... */
    assert ( vol . stop () == RAID_STOPPED );
    assert ( vol . status () == RAID_STOPPED );
    /* ... and the underlying disks. */
    doneDisks ();
}
//-------------------------------------------------------------------------------------------------
void                                   test2                                   ()
{
    /* The RAID as well as disks are stopped. It corresponds i.e. to the
     * restart of a real computer.
     *
     * after the restart, we will not create the disks, nor create RAID (we do not
     * want to destroy the content). Instead, we will only open/start the devices:
     */

    TBlkDev dev = openDisks ();
    CRaidVolume vol;

    assert ( vol . start ( dev ) == RAID_OK );


    /* some I/O: vol.read/vol.write
     */

    vol . stop ();
    doneDisks ();
}

void test3()
{

}


//-------------------------------------------------------------------------------------------------
int                                    main                                    ()
{
    test1 ();
//    test2 ();
    return EXIT_SUCCESS;
}

//

