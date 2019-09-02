#!/usr/bin/env bash

if [ "$TRAVIS_PULL_REQUEST" != "false" ]; then

    echo -e "Build Pull Request #$TRAVIS_PULL_REQUEST => Branch [$TRAVIS_BRANCH]"
    ./gradlew \
        -Pbranch="${TRAVIS_BRANCH}" \
        -PvcProtobufLibs="/c/Program Files/protobuf/lib" -PvcProtobufInclude="/c/Program Files/protobuf/include" \
        build

elif [ "$TRAVIS_PULL_REQUEST" == "false" ] && [ "$TRAVIS_TAG" == "" ] ; then

    echo -e "Build Branch => Branch ['$TRAVIS_BRANCH']"
    ./gradlew \
        -PbintrayUser="${bintrayUser}" -PbintrayKey="${bintrayKey}" \
        -Pbranch="${TRAVIS_BRANCH}" \
        -PvcProtobufLibs="/c/Program Files/protobuf/lib" -PvcProtobufInclude="/c/Program Files/protobuf/include" \
        build artifactoryPublish --stacktrace
else

    echo -e "Build Tag => Tag ['$TRAVIS_TAG']"
    ./gradlew \
        -PvcProtobufLibs="/c/Program Files/protobuf/lib" -PvcProtobufInclude="/c/Program Files/protobuf/include" \
        build

fi

