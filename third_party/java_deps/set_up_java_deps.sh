#!/usr/bin/env bash

#
#    Copyright (c) 2022-2023 Project CHIP Authors
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#

set -e

mkdir -p third_party/java_deps/artifacts

function download_jar() {
    _HOST=$1
    _MAVEN_PATH=$2
    _JAR_NAME=$3

    if [ ! -f "third_party/java_deps/artifacts/$_JAR_NAME" ]; then
        curl --fail --location --silent --show-error \
            "https://$_HOST/maven2/$_MAVEN_PATH/$_JAR_NAME" \
            -o "third_party/java_deps/artifacts/$_JAR_NAME"
    fi
}

download_jar "repo1.maven.org" "com/google/code/findbugs/jsr305/3.0.2" "jsr305-3.0.2.jar"
download_jar "repo1.maven.org" "com/google/code/gson/gson/2.9.1" "gson-2.9.1.jar"
download_jar "repo1.maven.org" "com/google/protobuf/protobuf-java/3.22.0" "protobuf-java-3.22.0.jar"
download_jar "repo1.maven.org" "com/google/truth/truth/1.1.3" "truth-1.1.3.jar"
download_jar "repo1.maven.org" "junit/junit/4.13.2" "junit-4.13.2.jar"
download_jar "repo1.maven.org" "org/jetbrains/kotlin/kotlin-stdlib/1.8.20" "kotlin-stdlib-1.8.20.jar"
download_jar "repo1.maven.org" "org/jetbrains/kotlin/kotlin-test/1.8.20" "kotlin-test-1.8.20.jar"
download_jar "repo1.maven.org" "org/jetbrains/kotlinx/kotlinx-coroutines-core-jvm/1.7.3" "kotlinx-coroutines-core-jvm-1.7.3.jar"
download_jar "repo1.maven.org" "org/json/json/20220924" "json-20220924.jar"

# Unit test requirements
download_jar "repo1.maven.org" "org/hamcrest/hamcrest-all/1.3" "hamcrest-all-1.3.jar"
download_jar "dist.wso2.org" "com/google/common/google-collect/1.0-rc1/" "google-collect-1.0-rc1.jar"

# Kotlin compiler (required for Android/Java controller builds that compile Kotlin sources)
KOTLIN_COMPILER_VERSION=1.9.24
KOTLIN_COMPILER_DIR=third_party/java_deps/kotlin_compiler
if [ ! -x "$KOTLIN_COMPILER_DIR/bin/kotlinc" ]; then
    echo "Downloading Kotlin compiler $KOTLIN_COMPILER_VERSION for controller Java build..."
    (cd third_party/java_deps/artifacts && \
        curl --fail --location --silent --show-error -o kotlin-compiler.zip \
            "https://github.com/JetBrains/kotlin/releases/download/v${KOTLIN_COMPILER_VERSION}/kotlin-compiler-${KOTLIN_COMPILER_VERSION}.zip" && \
        unzip -q -o kotlin-compiler.zip && \
        rm -f kotlin-compiler.zip)
    rm -rf "$KOTLIN_COMPILER_DIR"
    # GitHub zip extracts as "kotlinc", not kotlin-compiler-${version}
    mv "third_party/java_deps/artifacts/kotlinc" "$KOTLIN_COMPILER_DIR"
    chmod +x "$KOTLIN_COMPILER_DIR/bin/kotlinc" 2>/dev/null || true
    echo "Kotlin compiler installed at $KOTLIN_COMPILER_DIR"
fi
