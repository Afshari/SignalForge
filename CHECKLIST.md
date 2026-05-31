## README Maintenance Checklist

Run through this checklist whenever you add a feature, fix a bug, or change the project structure.

### Tests
- [ ] Update test count in Project Structure section (`SignalForge_Tests` line)
- [ ] Update test count in Tests section

### Pipeline changes
- [ ] Update pipeline architecture SVG (`docs/pipeline.svg`) if stages are added or removed
- [ ] Update Pipeline Architecture section description if behavior changes

### New features / flags
- [ ] Add new CLI flags to the Run section (`--newFlag`)
- [ ] Update DEVGUIDE.md with detailed usage

### Project structure
- [ ] Add new folders/files to the Project Structure tree if added to repo
- [ ] Remove entries for deleted folders/files

### Dependencies
- [ ] Update Requirements table if new dependencies are added
- [ ] Update Dockerfile and CMakeLists.txt accordingly

### Benchmarks
- [ ] Update Results section after running nsys/ncu profiling
- [ ] Add throughput numbers when available

### ML module
- [ ] Update SignalForge_ML/README.md if model architecture or results change

### Docker
- [ ] Verify `docker compose build` and `docker compose run signalforge` still work after changes