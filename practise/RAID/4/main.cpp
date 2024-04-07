#include <iostream>
#include <vector>

using namespace std;

class Disk {
public:
    Disk() = default;
    explicit Disk(int size) { data.resize(size); }

    unsigned char read(int index)
    {
        return data[index];
    }

    void write(int index, unsigned char value)
    {
        data[index] = value;
    }

private:
    vector<unsigned char> data;
};

class RAID5 {
public:
    RAID5(int numDisks, int diskSize)
    : numDisks(numDisks), dataSize(diskSize - 1), disks(numDisks - 1)
    {
        for (int i = 0; i < numDisks - 1; ++i)
            disks[i] = Disk(diskSize);

        parityDisk = Disk(diskSize);
    }

    bool diskIndexValid(int diskIndex) const
    {
        return diskIndex >= 0 && diskIndex < numDisks - 1;
    }

    // write one byte to the disk and updates parity
    void write(int diskIndex, int index, unsigned char value) {
        if (diskIndexValid(diskIndex))
        {
            disks[diskIndex].write(index, value);
            updateParity(index);
        }
        else
            cerr << "Invalid disk index for writing." << endl;
    }

    unsigned char read(int index) {
        unsigned char dataValue = 0;
        for (int i = 0; i < numDisks - 1; ++i) { // Exclude parity disk
            dataValue ^= disks[i].read(index); // XOR to recover original data
        }
        return dataValue;
    }

    void repairDisk(int diskIndex) {
        if (diskIndex < 0 || diskIndex >= numDisks - 1)
        {
            cerr << "Invalid disk index for repair." << endl;
            return;
        }

        for (int i = 0; i < dataSize; ++i)
        {
            unsigned char parityValue = parityDisk.read(i);
            for (int j = 0; j < numDisks - 1; ++j)
                if (j != diskIndex)
                    parityValue ^= disks[j].read(i);

            disks[diskIndex].write(i, parityValue);
        }
    }

    void printDisk(int diskIndex) {
        if (diskIndex >= 0 && diskIndex < numDisks) {
            if (diskIndex == numDisks - 1)
                cout << "Parity Disk: ";
            else
                cout << "Disk " << diskIndex << ": ";

            for (int i = 0; i < dataSize; ++i)
                if (diskIndex == numDisks - 1)
                    cout << static_cast<int>(parityDisk.read(i)) << " ";
                else
                    cout << static_cast<int>(disks[diskIndex].read(i)) << " ";

            cout << endl;
        }
        else
            cerr << "Invalid disk index for printing." << endl;
    }

    vector<Disk> & getDisks()
    {
        return disks;
    }

private:
    vector<Disk> disks;
    Disk parityDisk;

    int numDisks;
    int dataSize;

    void updateParity(int index)
    {
        unsigned char parityValue = 0;

        for (int i = 0; i < numDisks - 1; ++i)
            parityValue ^= disks[i].read(index);

        parityDisk.write(index, parityValue);
    }
};

int main() {
    RAID5 raid(4, 10); // 4 disks with each having 10 elements

    raid.write(0, 0, 'A');
    raid.write(1, 0, 'B');
    raid.write(2, 0, 'C');

    cout << "INITIAL DATA" << endl;

    for (int i = 0; i < 4; ++i)
        raid.printDisk(i);

    cout << "--------------------------------" << endl;

    cout << "Invalidating disk 2 writing 'X' (88) to pos 0" << endl;

    raid.getDisks()[2].write(0, 'X'); // Invalidating disk 2

    cout << "Disks after invalidating disk 2" << endl;

    for (int i = 0; i < 4; ++i)
        raid.printDisk(i);

    cout << "--------------------------------" << endl;

    raid.repairDisk(2); // Repairing disk 2

    cout << "Repairing disk 2" << endl;

    for (int i = 0; i < 4; ++i)
        raid.printDisk(i);

    return 0;
}
