run_unit_tests(){
    echo "Running unit tests..."
    ctest --verbose --test-dir ./build/Release  

    if [ $? -ne 0 ]; then
        echo "Unit tests failed. Exiting."
        exit 1
    fi
}