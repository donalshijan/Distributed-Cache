pipeline {
    agent any

    stages {
         stage('Install Dependencies') {
            steps {
                script {
                    // Install dependencies on the VM
                    sh '''
                    sudo apt-get update
                    sudo apt-get install -y git gcc cmake python3-pip
                    sudo pip3 install conan
                    '''
                }
            }
        }
        stage('Checkout') {
            steps {
                git branch: 'main', url: 'https://github.com/donalshijan/Distributed-Cache'
            }
        }

        stage('Build') {
            steps {
                script {
                    // Build the project using cmake
                    sh '''
                    pwd
                    ls
                    cd 'Build and test Distributed Cache'
                    cmake --preset conan-release
                    cmake --build --preset conan-release
                    '''
                }
            }
        }

        stage('Run Unit Tests') {
            steps {
                script {
                    // Run unit tests and stop if they fail
                    sh '''
                    pwd
                    ls
                    cd 'Build and test Distributed Cache'
                    ctest --verbose --test-dir ./build/Release
                    '''
                }
            }
        }
    }

    post {
        success {
            script {
                if (currentBuild.currentResult == 'SUCCESS') {
                    if (currentBuild.stageName == 'Build') {
                        // Notify GitHub that the build succeeded
                        githubNotify context: 'Build', status: 'SUCCESS', description: 'Build succeeded!'
                    } else if (currentBuild.stageName == 'Run Unit Tests') {
                        // Notify GitHub that the tests passed
                        githubNotify context: 'Tests', status: 'SUCCESS', description: 'All tests passed!'
                    }
                }
            }
        }

        failure {
            script {
                if (currentBuild.stageName == 'Build') {
                    // Notify GitHub that the build failed
                    githubNotify context: 'Build', status: 'FAILURE', description: 'Build failed.'
                } else if (currentBuild.stageName == 'Run Unit Tests') {
                    // Notify GitHub that the tests failed
                    githubNotify context: 'Tests', status: 'FAILURE', description: 'Tests failed.'
                }
            }
        }

        always {
            // Archive test results or artifacts
            archiveArtifacts artifacts: '**/target/*.log', allowEmptyArchive: true

            // Ensure all running processes are killed after the pipeline ends
            sh '''
            for PID in $(ps -e | grep distributed_cache | awk '{print $1}'); do
                kill -9 $PID || true
            done
            '''
        }
    }
}
