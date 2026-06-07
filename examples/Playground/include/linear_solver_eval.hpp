#pragma once

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>

#include <linear_system.hpp>
#include <linalg/linalg.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace linear_solver_eval {
    struct config {
        std::filesystem::path runtime_csv{"linear_solver_runtime.csv"};
        std::filesystem::path runtime_json{"linear_solver_runtime.json"};
        std::filesystem::path convergence_csv{"linear_solver_convergence.csv"};
        std::filesystem::path convergence_json{"linear_solver_convergence.json"};
        std::filesystem::path solution_csv{"linear_solver_x.csv"};
        std::filesystem::path solution_json{"linear_solver_x.json"};
        size_t solver_iterations{64};
        size_t convergence_iterations{64};
        double tolerance{1e-5};
    };

    struct dataset_info {
        std::string name;
        size_t rows{};
        size_t cols{};
        size_t non_zeros{};
        nlohmann::json header;
    };

    inline dataset_info make_dataset_info(const linear_system::system& input,
                                          std::string name = "linear_system") {
        return {
            std::move(name),
            input.rows,
            input.cols,
            linear_system::count_non_zero(input.A),
            input.header,
        };
    }

    inline void ensure_csv_open(const std::ofstream& out, const std::filesystem::path& path) {
        if (!out.is_open()) {
            throw std::runtime_error{"unable to open csv file: " + path.string()};
        }
    }

    inline void write_csv_string(std::ostream& out, const std::string& value) {
        out << '"';

        for (const auto c : value) {
            if (c == '"') {
                out << "\"\"";
            } else {
                out << c;
            }
        }

        out << '"';
    }

    inline double counter_value(const benchmark::UserCounters& counters, const std::string& name) {
        const auto counter = counters.find(name);

        if (counter == counters.end()) {
            return 0.0;
        }

        return counter->second.value;
    }

    template<typename VectorType>
        requires linalg::cpu_dense_vector_type<VectorType>
    struct solution_output {
        std::string storage;
        std::string solver;
        linalg::solver_result<VectorType> result;

        [[nodiscard]] std::string column_name() const {
            return storage + "_" + solver;
        }
    };

    class runtime_reporter final : public benchmark::BenchmarkReporter {
    public:
        runtime_reporter(std::ostream& csv_out,
                         std::ostream& json_out,
                         const dataset_info& source,
                         const config& cfg)
            : csv_out_{csv_out}, json_out_{json_out}, source_{source}, cfg_{cfg} {}

        bool ReportContext(const Context& context) override {
            csv_out_ << "matrix_rows,matrix_cols,non_zeros,storage,solver,benchmark_name,row_type,aggregate_name,"
                        "benchmark_iterations,real_time,cpu_time,time_unit,items_per_second,solver_iterations,"
                        "residual_norm,converged\n";
            json_ = {
                {"matrix",
                 {
                     {"rows", source_.rows},
                     {"cols", source_.cols},
                     {"non_zeros", source_.non_zeros},
                 }},
                {"options",
                 {
                     {"solver_iterations", cfg_.solver_iterations},
                     {"tolerance", cfg_.tolerance},
                 }},
                {"source", source_.name},
                {"generator", source_.header},
                {"benchmarks", nlohmann::json::array()},
            };

            return console_reporter_.ReportContext(context);
        }

        void ReportRunsConfig(double min_time, bool has_explicit_iters, benchmark::IterationCount iters) override {
            console_reporter_.ReportRunsConfig(min_time, has_explicit_iters, iters);
        }

        void ReportRuns(const std::vector<Run>& reports) override {
            console_reporter_.ReportRuns(reports);

            for (const auto& report : reports) {
                write_report_row(report);
            }
        }

        void Finalize() override {
            console_reporter_.Finalize();
            json_out_ << json_.dump(2) << '\n';
            csv_out_.flush();
            json_out_.flush();
        }

    private:
        void write_report_row(const Run& report) {
            const auto name = report.benchmark_name();
            const auto split = name.find('/');
            const auto storage = split == std::string::npos ? std::string{} : name.substr(0, split);
            const auto solver = split == std::string::npos ? name : name.substr(split + 1);
            const auto row_type = report.run_type == Run::RT_Iteration ? "iteration" : "aggregate";
            const auto items_per_second = report.real_accumulated_time == 0.0
                                              ? 0.0
                                              : static_cast<double>(report.iterations * source_.rows) /
                                                    report.real_accumulated_time;

            csv_out_ << source_.rows << ','
                     << source_.cols << ','
                     << source_.non_zeros << ',';
            write_csv_string(csv_out_, storage);
            csv_out_ << ',';
            write_csv_string(csv_out_, solver);
            csv_out_ << ',';
            write_csv_string(csv_out_, name);
            csv_out_ << ',';
            write_csv_string(csv_out_, row_type);
            csv_out_ << ',';
            write_csv_string(csv_out_, report.aggregate_name);
            csv_out_ << ','
                     << report.iterations << ','
                     << report.GetAdjustedRealTime() << ','
                     << report.GetAdjustedCPUTime() << ',';
            write_csv_string(csv_out_, benchmark::GetTimeUnitString(report.time_unit));
            csv_out_ << ','
                     << items_per_second << ','
                     << counter_value(report.counters, "iterations") << ','
                     << counter_value(report.counters, "residual_norm") << ','
                     << counter_value(report.counters, "converged") << '\n';

            json_["benchmarks"].push_back({
                {"matrix_rows", source_.rows},
                {"matrix_cols", source_.cols},
                {"non_zeros", source_.non_zeros},
                {"storage", storage},
                {"solver", solver},
                {"benchmark_name", name},
                {"row_type", row_type},
                {"aggregate_name", report.aggregate_name},
                {"benchmark_iterations", report.iterations},
                {"real_time", report.GetAdjustedRealTime()},
                {"cpu_time", report.GetAdjustedCPUTime()},
                {"time_unit", benchmark::GetTimeUnitString(report.time_unit)},
                {"items_per_second", items_per_second},
                {"solver_iterations", counter_value(report.counters, "iterations")},
                {"residual_norm", counter_value(report.counters, "residual_norm")},
                {"converged", counter_value(report.counters, "converged") != 0.0},
            });
        }

        std::ostream& csv_out_;
        std::ostream& json_out_;
        const dataset_info& source_;
        const config& cfg_;
        benchmark::ConsoleReporter console_reporter_{};
        nlohmann::json json_;
    };

    template<typename MatrixType, typename VectorType, typename Solver>
        requires linalg::detail::linear_system_type<MatrixType, VectorType>
    void register_solver_benchmark(const std::string& name,
                                   const MatrixType& A,
                                   const VectorType& b,
                                   linalg::solver_options options,
                                   Solver&& solver) {
        benchmark::RegisterBenchmark(
            name.c_str(),
            [&A, &b, options, solver = std::forward<Solver>(solver)](benchmark::State& state) {
                linalg::solver_result<VectorType> result{};

                for (auto _ : state) {
                    result = solver(A, b, options);
                    benchmark::DoNotOptimize(result.x.data.data());
                    benchmark::ClobberMemory();
                }

                state.counters["iterations"] = benchmark::Counter(static_cast<double>(result.iterations));
                state.counters["residual_norm"] = benchmark::Counter(result.residual_norm);
                state.counters["converged"] = benchmark::Counter(result.converged ? 1.0 : 0.0);
                state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * MatrixType::rows));
            })
            ->Unit(benchmark::kMillisecond);
    }

    template<typename DenseMatrixType, typename CSRMatrixType, typename VectorType>
        requires linalg::detail::linear_system_type<DenseMatrixType, VectorType> &&
                 linalg::detail::linear_system_type<CSRMatrixType, VectorType>
    void write_convergence_outputs(const dataset_info& source,
                                   const DenseMatrixType& dense,
                                   const CSRMatrixType& csr,
                                   const VectorType& b,
                                   const config& cfg) {
        std::ofstream csv_out{cfg.convergence_csv};
        ensure_csv_open(csv_out, cfg.convergence_csv);

        std::ofstream json_out{cfg.convergence_json};
        ensure_csv_open(json_out, cfg.convergence_json);

        csv_out << "matrix_rows,matrix_cols,non_zeros,storage,solver,iteration,residual_norm,converged,elapsed_ns\n";
        auto json = nlohmann::json{
            {"matrix",
             {
                 {"rows", source.rows},
                 {"cols", source.cols},
                 {"non_zeros", source.non_zeros},
             }},
            {"options",
             {
                 {"max_iterations", cfg.convergence_iterations},
                 {"tolerance", cfg.tolerance},
             }},
            {"source", source.name},
            {"generator", source.header},
            {"samples", nlohmann::json::array()},
        };

        const auto write_source_prefix = [&] {
            csv_out << source.rows << ','
                    << source.cols << ','
                    << source.non_zeros << ',';
        };

        const auto write_rows = [&](const auto& A, const std::string& storage) {
            const auto emit = [&](const std::string& solver_name, auto&& solver) {
                for (size_t iteration = 1; iteration <= cfg.convergence_iterations; ++iteration) {
                    const auto start = std::chrono::steady_clock::now();
                    const auto result = solver(A, b, linalg::solver_options{iteration, -1.0});
                    const auto end = std::chrono::steady_clock::now();
                    const auto elapsed_ns =
                        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

                    write_source_prefix();
                    csv_out << storage << ','
                            << solver_name << ','
                            << iteration << ','
                            << result.residual_norm << ','
                            << (result.residual_norm <= cfg.tolerance ? 1 : 0) << ','
                            << elapsed_ns << '\n';

                    json["samples"].push_back({
                        {"matrix_rows", source.rows},
                        {"matrix_cols", source.cols},
                        {"non_zeros", source.non_zeros},
                        {"storage", storage},
                        {"solver", solver_name},
                        {"iteration", iteration},
                        {"residual_norm", result.residual_norm},
                        {"converged", result.residual_norm <= cfg.tolerance},
                        {"elapsed_ns", elapsed_ns},
                    });
                }
            };

            emit("cg", [](const auto& matrix, const auto& rhs, linalg::solver_options options) {
                return linalg::cg(matrix, rhs, options);
            });
            emit("pcg", [](const auto& matrix, const auto& rhs, linalg::solver_options options) {
                return linalg::pcg(matrix, rhs, options);
            });
            emit("jacobi", [](const auto& matrix, const auto& rhs, linalg::solver_options options) {
                return linalg::jacobi(matrix, rhs, options);
            });
            emit("gauss_seidel", [](const auto& matrix, const auto& rhs, linalg::solver_options options) {
                return linalg::gauss_seidel(matrix, rhs, options);
            });
        };

        write_rows(dense, "dense_row_major");
        write_rows(csr, "csr");

        json_out << json.dump(2) << '\n';
    }

    template<typename DenseMatrixType, typename CSRMatrixType, typename VectorType>
        requires linalg::detail::linear_system_type<DenseMatrixType, VectorType> &&
                 linalg::detail::linear_system_type<CSRMatrixType, VectorType>
    void write_solution_outputs(const dataset_info& source,
                                const DenseMatrixType& dense,
                                const CSRMatrixType& csr,
                                const VectorType& b,
                                const config& cfg,
                                linalg::solver_options options) {
        std::vector<solution_output<VectorType>> outputs;
        outputs.reserve(8);

        const auto collect = [&](const auto& A, const std::string& storage) {
            outputs.push_back({storage, "cg", linalg::cg(A, b, options)});
            outputs.push_back({storage, "pcg", linalg::pcg(A, b, options)});
            outputs.push_back({storage, "jacobi", linalg::jacobi(A, b, options)});
            outputs.push_back({storage, "gauss_seidel", linalg::gauss_seidel(A, b, options)});
        };

        collect(dense, "dense_row_major");
        collect(csr, "csr");

        std::ofstream csv_out{cfg.solution_csv};
        ensure_csv_open(csv_out, cfg.solution_csv);

        csv_out << "index,b";

        for (const auto& output : outputs) {
            csv_out << ',';
            write_csv_string(csv_out, output.column_name());
        }

        csv_out << '\n';

        for (size_t i = 0; i < VectorType::rows; ++i) {
            csv_out << i << ',' << b(i);

            for (const auto& output : outputs) {
                csv_out << ',' << output.result.x(i);
            }

            csv_out << '\n';
        }

        auto json = nlohmann::json{
            {"matrix",
             {
                 {"rows", source.rows},
                 {"cols", source.cols},
                 {"non_zeros", source.non_zeros},
             }},
            {"options",
             {
                 {"max_iterations", options.max_iterations},
                 {"tolerance", options.tolerance},
             }},
            {"source", source.name},
            {"generator", source.header},
            {"b", nlohmann::json::array()},
            {"solutions", nlohmann::json::array()},
        };

        for (size_t i = 0; i < VectorType::rows; ++i) {
            json["b"].push_back(b(i));
        }

        for (const auto& output : outputs) {
            auto x = nlohmann::json::array();

            for (size_t i = 0; i < VectorType::rows; ++i) {
                x.push_back(output.result.x(i));
            }

            json["solutions"].push_back({
                {"storage", output.storage},
                {"solver", output.solver},
                {"column_name", output.column_name()},
                {"iterations", output.result.iterations},
                {"residual_norm", output.result.residual_norm},
                {"converged", output.result.converged},
                {"x", std::move(x)},
            });
        }

        std::ofstream json_out{cfg.solution_json};
        ensure_csv_open(json_out, cfg.solution_json);
        json_out << json.dump(2) << '\n';
    }

    template<typename DenseMatrixType, typename CSRMatrixType, typename VectorType>
        requires linalg::cpu_dense_matrix_type<DenseMatrixType> &&
                 linalg::cpu_sparse_matrix_type<CSRMatrixType> &&
                 linalg::detail::linear_system_type<DenseMatrixType, VectorType> &&
                 linalg::detail::linear_system_type<CSRMatrixType, VectorType> &&
                 (DenseMatrixType::rows == CSRMatrixType::rows) &&
                 (DenseMatrixType::cols == CSRMatrixType::cols) &&
                 (DenseMatrixType::rows == DenseMatrixType::cols)
    void run(const dataset_info& source,
             const DenseMatrixType& dense,
             const CSRMatrixType& csr,
             const VectorType& b,
             int argc,
             char** argv,
             const config& cfg = {}) {
        const linalg::solver_options options{cfg.solver_iterations, cfg.tolerance};

        benchmark::Initialize(&argc, argv);
        benchmark::AddCustomContext("source", source.name);
        benchmark::AddCustomContext("matrix_rows", std::to_string(source.rows));
        benchmark::AddCustomContext("matrix_cols", std::to_string(source.cols));
        benchmark::AddCustomContext("non_zeros", std::to_string(source.non_zeros));
        benchmark::AddCustomContext("solver_iterations", std::to_string(cfg.solver_iterations));
        benchmark::AddCustomContext("tolerance", std::to_string(cfg.tolerance));

        register_solver_benchmark("dense_row_major/cg", dense, b, options, [](const auto& A, const auto& rhs, auto opts) {
            return linalg::cg(A, rhs, opts);
        });
        register_solver_benchmark("dense_row_major/pcg", dense, b, options, [](const auto& A, const auto& rhs, auto opts) {
            return linalg::pcg(A, rhs, opts);
        });
        register_solver_benchmark("dense_row_major/jacobi", dense, b, options, [](const auto& A, const auto& rhs, auto opts) {
            return linalg::jacobi(A, rhs, opts);
        });
        register_solver_benchmark("dense_row_major/gauss_seidel", dense, b, options, [](const auto& A, const auto& rhs, auto opts) {
            return linalg::gauss_seidel(A, rhs, opts);
        });

        register_solver_benchmark("csr/cg", csr, b, options, [](const auto& A, const auto& rhs, auto opts) {
            return linalg::cg(A, rhs, opts);
        });
        register_solver_benchmark("csr/pcg", csr, b, options, [](const auto& A, const auto& rhs, auto opts) {
            return linalg::pcg(A, rhs, opts);
        });
        register_solver_benchmark("csr/jacobi", csr, b, options, [](const auto& A, const auto& rhs, auto opts) {
            return linalg::jacobi(A, rhs, opts);
        });
        register_solver_benchmark("csr/gauss_seidel", csr, b, options, [](const auto& A, const auto& rhs, auto opts) {
            return linalg::gauss_seidel(A, rhs, opts);
        });

        write_convergence_outputs(source, dense, csr, b, cfg);
        write_solution_outputs(source, dense, csr, b, cfg, options);

        std::ofstream runtime_out{cfg.runtime_csv};
        ensure_csv_open(runtime_out, cfg.runtime_csv);
        std::ofstream runtime_json{cfg.runtime_json};
        ensure_csv_open(runtime_json, cfg.runtime_json);

        runtime_reporter reporter{runtime_out, runtime_json, source, cfg};
        benchmark::RunSpecifiedBenchmarks(&reporter);
        benchmark::Shutdown();
    }
}
