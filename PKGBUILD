# Maintainer: User <me@kam1k4dze.com>
pkgname=inspect-deps
pkgver=0.1.0
pkgrel=1
pkgdesc="ELF dependency analyzer for Arch Linux"
arch=('any')
url="https://github.com/Kamk14dze/inspect-deps"
license=('MIT')
depends=('python' 'binutils' 'pacman')
makedepends=('python-build' 'python-installer' 'python-wheel' 'python-hatchling')
source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/v$pkgver.tar.gz")
sha256sums=('SKIP')

build() {
  cd "$pkgname-$pkgver"
  python -m build --wheel --no-isolation
}

package() {
  cd "$pkgname-$pkgver"
  python -m installer --destdir="$pkgdir" dist/*.whl
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}

