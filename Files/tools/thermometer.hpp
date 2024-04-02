#pragma once
#include <vector>
#include <numeric>
#include <fstream>

namespace thermometer
{

    template <typename T>
    class Measure
    {
    public:
        Measure()
        {
            mutex_ = sg4::Mutex::create();
        }
        void add_to_series(const T &value)
        {
            std::lock_guard lock(*mutex_);
            series.push_back(value);
        }
        virtual void save_series_to_file(const std::string &label, const std::string &filename)
        {
            std::ofstream file(filename, std::ios_base::app);
            file << label << '\n';
            for (auto &value : series)
            {
                file << value << '\n';
            }
            file.close();
        }

    protected:
        std::vector<T> series;
        sg4::MutexPtr mutex_;
    };

    template <typename T>
    class AggregateMean : public Measure<T>
    {
    public:
        void save_series_to_file(const std::string &label, const std::string &filename) override
        {
            std::ofstream file(filename, std::ios_base::app);

            T sum = std::accumulate(this->series.begin(), this->series.end(), 0);

            file << label << ": " << sum / this->series.size() << '\n';
            file.close();
        }
    };

    template <typename T>
    class AggregateMeanSecondsToMinutes : public Measure<T>
    {
    public:
        void save_series_to_file(const std::string &label, const std::string &filename) override
        {
            std::ofstream file(filename, std::ios_base::app);

            T sum = std::accumulate(this->series.begin(), this->series.end(), 0);

            file << label << ": " << sum / 60.0 / this->series.size() << '\n';
            file.close();
        }
    };

} // namespace thermometer
